#include "signal_handler.hpp"
#include <csignal>
#include <iostream>

namespace anolis
{
    namespace runtime
    {

        SignalHandler::Callback SignalHandler::callback_;

        void SignalHandler::install(Callback callback)
        {
            callback_ = callback;
            std::signal(SIGINT, handle_signal);
            std::signal(SIGTERM, handle_signal);
        }

        void SignalHandler::handle_signal(int signal)
        {
            if (callback_)
            {
                callback_();
            }
        }

    } // namespace runtime
} // namespace anolis