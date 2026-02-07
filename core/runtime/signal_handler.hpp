#pragma once

#include <atomic>

namespace anolis {
namespace runtime {

class SignalHandler {
public:
    static void install();
    static bool is_shutdown_requested();

private:
    static void handle_signal(int signal);
    static std::atomic<bool> shutdown_requested_;
};

}  // namespace runtime
}  // namespace anolis