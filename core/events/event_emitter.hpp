#pragma once

/**
 * @file event_emitter.hpp
 * @brief Thread-safe fan-out event dispatcher with per-subscriber queues
 *
 * Architecture:
 * - StateCache emits events to a single EventEmitter instance
 * - Each subscriber (SSE client, telemetry sink) gets its own bounded queue
 * - Fan-out is non-blocking: slow subscribers don't block StateCache or others
 * - Overflow drops oldest events per-subscriber with warning log
 *
 * Thread safety:
 * - emit() is called from polling thread (StateCache)
 * - subscribe()/unsubscribe() called from HTTP threads (SSE handlers)
 * - pop() called from subscriber threads (SSE handler loop, telemetry flush)
 *
 * All operations are mutex-protected with minimal critical sections.
 */

#include <atomic>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <unordered_map>

#include "event_types.hpp"

namespace anolis {
namespace events {

/**
 * @brief Bounded event queue for a single subscriber
 *
 * Each subscriber gets their own queue with independent overflow handling.
 * This isolates slow consumers from affecting others.
 */
class SubscriberQueue {
public:
    explicit SubscriberQueue(size_t max_size, const std::string &name = "")
        : max_size_(max_size), name_(name), dropped_count_(0) {}

    /**
     * @brief Push event to queue (producer side)
     *
     * If queue is full, drops oldest event and logs warning.
     * Never blocks - always returns immediately.
     *
     * @param event Event to push
     * @return true if pushed, false if dropped (queue was full, oldest dropped)
     */
    bool push(const Event &event) {
        std::lock_guard<std::mutex> lock(mutex_);

        // If at capacity, drop oldest
        if (queue_.size() >= max_size_) {
            queue_.pop();
            dropped_count_++;

            // Log warning periodically (every 100 drops)
            if (dropped_count_ % 100 == 1) {
                std::cerr << "[EventEmitter] Queue '" << name_ << "' overflow, dropped " << dropped_count_
                          << " events total\n";
            }
        }

        queue_.push(event);
        cv_.notify_one();
        return dropped_count_ == 0 || (queue_.size() < max_size_);
    }

    /**
     * @brief Pop event from queue (consumer side)
     *
     * Blocks until event is available or timeout expires.
     *
     * @param timeout_ms Max time to wait (0 = non-blocking)
     * @return Event if available, std::nullopt on timeout
     */
    std::optional<Event> pop(int timeout_ms = 0) {
        std::unique_lock<std::mutex> lock(mutex_);

        if (timeout_ms > 0) {
            cv_.wait_for(lock, std::chrono::milliseconds(timeout_ms), [this] { return !queue_.empty() || closed_; });
        }

        if (queue_.empty()) {
            return std::nullopt;
        }

        Event event = std::move(queue_.front());
        queue_.pop();
        return event;
    }

    /**
     * @brief Try to pop without blocking
     */
    std::optional<Event> try_pop() { return pop(0); }

    /**
     * @brief Get current queue size
     */
    size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.size();
    }

    /**
     * @brief Check if queue is empty
     */
    bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return queue_.empty();
    }

    /**
     * @brief Get total dropped event count
     */
    size_t dropped_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return dropped_count_;
    }

    /**
     * @brief Close queue (unblocks waiting consumers)
     */
    void close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        cv_.notify_all();
    }

    /**
     * @brief Check if queue is closed
     */
    bool is_closed() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_;
    }

private:
    const size_t max_size_;
    const std::string name_;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::queue<Event> queue_;
    size_t dropped_count_;
    bool closed_ = false;
};

/**
 * @brief Subscription handle returned to subscribers
 *
 * Subscribers hold this to receive events and unsubscribe.
 * RAII: unsubscribes automatically on destruction.
 */
class Subscription {
public:
    using SubscriptionId = uint64_t;

    Subscription(SubscriptionId id, std::shared_ptr<SubscriberQueue> queue,
                 std::function<void(SubscriptionId)> unsubscribe_fn)
        : id_(id), queue_(queue), unsubscribe_fn_(unsubscribe_fn) {}

    ~Subscription() { unsubscribe(); }

    // Non-copyable (unique ownership)
    Subscription(const Subscription &) = delete;
    Subscription &operator=(const Subscription &) = delete;

    // Movable
    Subscription(Subscription &&other) noexcept
        : id_(other.id_), queue_(std::move(other.queue_)), unsubscribe_fn_(std::move(other.unsubscribe_fn_)) {
        other.id_ = 0;
    }

    Subscription &operator=(Subscription &&other) noexcept {
        if (this != &other) {
            unsubscribe();
            id_ = other.id_;
            queue_ = std::move(other.queue_);
            unsubscribe_fn_ = std::move(other.unsubscribe_fn_);
            other.id_ = 0;
        }
        return *this;
    }

    /**
     * @brief Pop next event (blocks up to timeout_ms)
     */
    std::optional<Event> pop(int timeout_ms = 100) {
        if (!queue_) return std::nullopt;
        return queue_->pop(timeout_ms);
    }

    /**
     * @brief Try pop without blocking
     */
    std::optional<Event> try_pop() {
        if (!queue_) return std::nullopt;
        return queue_->try_pop();
    }

    /**
     * @brief Get subscription ID
     */
    SubscriptionId id() const { return id_; }

    /**
     * @brief Check if still subscribed
     */
    bool is_active() const { return queue_ != nullptr && !queue_->is_closed(); }

    /**
     * @brief Get queue size
     */
    size_t queue_size() const { return queue_ ? queue_->size() : 0; }

    /**
     * @brief Get dropped count
     */
    size_t dropped_count() const { return queue_ ? queue_->dropped_count() : 0; }

    /**
     * @brief Manually unsubscribe
     */
    void unsubscribe() {
        if (id_ != 0 && unsubscribe_fn_) {
            unsubscribe_fn_(id_);
            id_ = 0;
            if (queue_) {
                queue_->close();
            }
        }
    }

private:
    SubscriptionId id_;
    std::shared_ptr<SubscriberQueue> queue_;
    std::function<void(SubscriptionId)> unsubscribe_fn_;
};

/**
 * @brief Event filter for subscribers
 *
 * Allows filtering events by provider/device/signal.
 * Empty filter = receive all events.
 */
struct EventFilter {
    std::string provider_id;  // Empty = all providers
    std::string device_id;    // Empty = all devices
    std::string signal_id;    // Empty = all signals

    /**
     * @brief Check if event matches filter
     */
    bool matches(const Event &event) const {
        return std::visit(
            [this](auto &&e) -> bool {
                using T = std::decay_t<decltype(e)>;

                // Check provider_id filter
                if (!provider_id.empty()) {
                    if constexpr (std::is_same_v<T, StateUpdateEvent> || std::is_same_v<T, QualityChangeEvent> ||
                                  std::is_same_v<T, DeviceAvailabilityEvent>) {
                        if (e.provider_id != provider_id) return false;
                    }
                }

                // Check device_id filter
                if (!device_id.empty()) {
                    if constexpr (std::is_same_v<T, StateUpdateEvent> || std::is_same_v<T, QualityChangeEvent> ||
                                  std::is_same_v<T, DeviceAvailabilityEvent>) {
                        if (e.device_id != device_id) return false;
                    }
                }

                // Check signal_id filter (only applies to signal events)
                if (!signal_id.empty()) {
                    if constexpr (std::is_same_v<T, StateUpdateEvent> || std::is_same_v<T, QualityChangeEvent>) {
                        if (e.signal_id != signal_id) return false;
                    }
                }

                return true;
            },
            event);
    }

    /**
     * @brief Create filter that matches all events
     */
    static EventFilter all() { return EventFilter{}; }
};

/**
 * @brief Thread-safe event emitter with fan-out to per-subscriber queues
 *
 * Central hub for event distribution. StateCache calls emit() on changes,
 * and all subscribed consumers receive a copy in their own queue.
 */
class EventEmitter {
public:
    using SubscriptionId = Subscription::SubscriptionId;

    /**
     * @brief Create emitter with default queue sizes
     *
     * @param default_queue_size Default max events per subscriber queue
     * @param max_subscribers Maximum concurrent subscribers (0 = unlimited)
     */
    explicit EventEmitter(size_t default_queue_size = 100, size_t max_subscribers = 32)
        : default_queue_size_(default_queue_size),
          max_subscribers_(max_subscribers),
          next_subscription_id_(1),
          next_event_id_(1) {}

    /**
     * @brief Subscribe to events
     *
     * Creates a new subscription with its own bounded queue.
     *
     * @param filter Event filter (empty = all events)
     * @param queue_size Override default queue size (0 = use default)
     * @param name Debug name for logging
     * @return Subscription handle, or nullptr if max subscribers reached
     */
    std::unique_ptr<Subscription> subscribe(const EventFilter &filter = EventFilter::all(), size_t queue_size = 0,
                                            const std::string &name = "") {
        std::lock_guard<std::mutex> lock(mutex_);

        // Check subscriber limit
        if (max_subscribers_ > 0 && subscribers_.size() >= max_subscribers_) {
            std::cerr << "[EventEmitter] Max subscribers (" << max_subscribers_
                      << ") reached, rejecting subscription\n";
            return nullptr;
        }

        auto id = next_subscription_id_++;
        auto actual_queue_size = queue_size > 0 ? queue_size : default_queue_size_;
        auto queue = std::make_shared<SubscriberQueue>(actual_queue_size, name);

        subscribers_[id] = SubscriberInfo{queue, filter, name};

        std::cerr << "[EventEmitter] Subscription " << id << " created" << (name.empty() ? "" : " (" + name + ")")
                  << ", total subscribers: " << subscribers_.size() << "\n";

        auto unsubscribe_fn = [this](SubscriptionId sub_id) { this->unsubscribe(sub_id); };

        return std::make_unique<Subscription>(id, queue, unsubscribe_fn);
    }

    /**
     * @brief Emit event to all subscribers
     *
     * Assigns monotonic event_id and fans out to all matching subscribers.
     * Non-blocking: each subscriber's queue handles its own overflow.
     *
     * @param event Event to emit (event_id will be assigned)
     */
    void emit(Event event) {
        std::lock_guard<std::mutex> lock(mutex_);

        // Assign monotonic event ID
        uint64_t id = next_event_id_++;
        std::visit([id](auto &&e) { e.event_id = id; }, event);

        // Fan out to all matching subscribers
        for (auto &[sub_id, info] : subscribers_) {
            if (info.filter.matches(event)) {
                info.queue->push(event);
            }
        }
    }

    /**
     * @brief Get next event ID (for external use if needed)
     */
    uint64_t next_event_id() const { return next_event_id_.load(); }

    /**
     * @brief Get current subscriber count
     */
    size_t subscriber_count() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return subscribers_.size();
    }

    /**
     * @brief Get max subscribers limit
     */
    size_t max_subscribers() const { return max_subscribers_; }

    /**
     * @brief Check if at capacity
     */
    bool at_capacity() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return max_subscribers_ > 0 && subscribers_.size() >= max_subscribers_;
    }

private:
    void unsubscribe(SubscriptionId id) {
        std::lock_guard<std::mutex> lock(mutex_);

        auto it = subscribers_.find(id);
        if (it != subscribers_.end()) {
            it->second.queue->close();
            subscribers_.erase(it);
            std::cerr << "[EventEmitter] Subscription " << id << " removed, remaining: " << subscribers_.size() << "\n";
        }
    }

    struct SubscriberInfo {
        std::shared_ptr<SubscriberQueue> queue;
        EventFilter filter;
        std::string name;
    };

    const size_t default_queue_size_;
    const size_t max_subscribers_;

    mutable std::mutex mutex_;
    std::unordered_map<SubscriptionId, SubscriberInfo> subscribers_;
    std::atomic<SubscriptionId> next_subscription_id_;
    std::atomic<uint64_t> next_event_id_;
};

}  // namespace events
}  // namespace anolis
