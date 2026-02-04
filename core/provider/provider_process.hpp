#pragma once

#include "framed_stdio_client.hpp"
#include <string>
#include <memory>

namespace anolis
{
    namespace provider
    {

        // ProviderProcess manages the lifecycle of a provider child process
        // Responsibilities:
        // - Spawn process with redirected stdin/stdout
        // - Monitor process health
        // - Clean/forced shutdown
        class ProviderProcess
        {
        public:
            ProviderProcess(const std::string &provider_id, const std::string &executable_path,
                            const std::vector<std::string> &args = {});
            ~ProviderProcess();

            // Delete copy/move
            ProviderProcess(const ProviderProcess &) = delete;
            ProviderProcess &operator=(const ProviderProcess &) = delete;

            // Spawn the provider process
            // Returns true on success, false on failure (sets error_)
            bool spawn();

            // Check if process is still running
            bool is_running() const;

            // Shutdown sequence: EOF -> wait -> kill
            void shutdown();

            // Get framed client for communication
            FramedStdioClient &client() { return client_; }

            // Get provider ID
            const std::string &provider_id() const { return provider_id_; }

            // Get last error
            const std::string &last_error() const { return error_; }

        private:
            std::string provider_id_;
            std::string executable_path_;
            std::vector<std::string> args_;
            std::string error_;

            FramedStdioClient client_;

#ifdef _WIN32
            void *process_handle_; // HANDLE
            void *stdin_read_;     // HANDLE (child's stdin read)
            void *stdout_write_;   // HANDLE (child's stdout write)
#else
            pid_t pid_;
            int stdin_write_fd_;
            int stdout_read_fd_;
#endif

            bool spawn_windows();
            bool spawn_linux();
            bool wait_for_exit(int timeout_ms);
            void force_terminate();
        };

    } // namespace provider
} // namespace anolis
