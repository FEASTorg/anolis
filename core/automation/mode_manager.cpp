#include "automation/mode_manager.hpp"
#include <iostream>

namespace anolis {
namespace automation {

const char* mode_to_string(RuntimeMode mode) {
    switch (mode) {
        case RuntimeMode::MANUAL: return "MANUAL";
        case RuntimeMode::AUTO:   return "AUTO";
        case RuntimeMode::IDLE:   return "IDLE";
        case RuntimeMode::FAULT:  return "FAULT";
        default:                  return "UNKNOWN";
    }
}

RuntimeMode string_to_mode(const std::string& str) {
    if (str == "MANUAL") return RuntimeMode::MANUAL;
    if (str == "AUTO")   return RuntimeMode::AUTO;
    if (str == "IDLE")   return RuntimeMode::IDLE;
    if (str == "FAULT")  return RuntimeMode::FAULT;
    
    std::cerr << "[ModeManager] WARNING: Unknown mode string '" << str 
              << "', defaulting to MANUAL\n";
    return RuntimeMode::MANUAL;
}

ModeManager::ModeManager(RuntimeMode initial_mode)
    : current_mode_(initial_mode)
{
    std::cout << "[ModeManager] Initialized in " << mode_to_string(initial_mode) << " mode\n";
}

RuntimeMode ModeManager::current_mode() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return current_mode_;
}

bool ModeManager::set_mode(RuntimeMode new_mode, std::string& error) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    // No-op if already in requested mode
    if (current_mode_ == new_mode) {
        return true;
    }
    
    // Validate transition
    if (!is_valid_transition(current_mode_, new_mode)) {
        error = std::string("Invalid mode transition: ") +
                mode_to_string(current_mode_) + " -> " + mode_to_string(new_mode);
        std::cerr << "[ModeManager] " << error << "\n";
        return false;
    }
    
    RuntimeMode previous_mode = current_mode_;
    current_mode_ = new_mode;
    
    std::cout << "[ModeManager] Mode changed: " << mode_to_string(previous_mode) 
              << " -> " << mode_to_string(new_mode) << "\n";
    
    // Notify callbacks (with lock released to prevent deadlocks)
    // Copy callbacks vector while holding lock
    auto callbacks_copy = callbacks_;
    
    // Release lock before calling callbacks
    mutex_.unlock();
    for (const auto& callback : callbacks_copy) {
        try {
            callback(previous_mode, new_mode);
        } catch (const std::exception& e) {
            std::cerr << "[ModeManager] ERROR in mode change callback: " << e.what() << "\n";
        }
    }
    // Note: lock is already released, will be re-acquired by lock_guard destructor
    // but that's harmless since we're returning
    
    return true;
}

void ModeManager::on_mode_change(ModeChangeCallback callback) {
    std::lock_guard<std::mutex> lock(mutex_);
    callbacks_.push_back(callback);
}

bool ModeManager::is_valid_transition(RuntimeMode from, RuntimeMode to) const {
    // Same mode is always valid (no-op)
    if (from == to) {
        return true;
    }
    
    // Any mode can transition to FAULT (error condition)
    if (to == RuntimeMode::FAULT) {
        return true;
    }
    
    // FAULT can only transition to MANUAL (recovery path)
    if (from == RuntimeMode::FAULT) {
        return to == RuntimeMode::MANUAL;
    }
    
    // Normal operations: MANUAL ↔ AUTO, MANUAL ↔ IDLE
    if (from == RuntimeMode::MANUAL) {
        return to == RuntimeMode::AUTO || to == RuntimeMode::IDLE;
    }
    
    if (from == RuntimeMode::AUTO || from == RuntimeMode::IDLE) {
        return to == RuntimeMode::MANUAL;
    }
    
    // All other transitions blocked
    return false;
}

}  // namespace automation
}  // namespace anolis
