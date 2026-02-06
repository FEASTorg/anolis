#pragma once

#include <functional>
#include <atomic>

namespace anolis
{
    namespace runtime
    {

        class SignalHandler
        {
        public:
            using Callback = std::function<void()>;

            static void install(Callback callback);

        private:
            static void handle_signal(int signal);
            static Callback callback_;
        };

    } // namespace runtime
} // namespace anolis