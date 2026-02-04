#include "provider_process.hpp"
#include <iostream>
#include <filesystem>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#endif

namespace anolis
{
    namespace provider
    {

        ProviderProcess::ProviderProcess(const std::string &provider_id, const std::string &executable_path,
                                         const std::vector<std::string> &args)
            : provider_id_(provider_id), executable_path_(executable_path), args_(args)
#ifdef _WIN32
              ,
              process_handle_(nullptr), stdin_read_(nullptr), stdout_write_(nullptr)
#else
              ,
              pid_(-1), stdin_write_fd_(-1), stdout_read_fd_(-1)
#endif
        {
        }

        ProviderProcess::~ProviderProcess()
        {
            shutdown();
        }

        bool ProviderProcess::spawn()
        {
            std::cerr << "[" << provider_id_ << "] Spawning: " << executable_path_ << "\n";

            // Check executable exists
            if (!std::filesystem::exists(executable_path_))
            {
                error_ = "Executable not found: " + executable_path_;
                std::cerr << "[" << provider_id_ << "] ERROR: " << error_ << "\n";
                return false;
            }

#ifdef _WIN32
            return spawn_windows();
#else
            return spawn_linux();
#endif
        }

#ifdef _WIN32
        bool ProviderProcess::spawn_windows()
        {
            // Create pipes with inheritable handles
            SECURITY_ATTRIBUTES sa = {};
            sa.nLength = sizeof(SECURITY_ATTRIBUTES);
            sa.bInheritHandle = TRUE;
            sa.lpSecurityDescriptor = NULL;

            HANDLE stdin_read_child = NULL;
            HANDLE stdin_write_parent = NULL;
            HANDLE stdout_read_parent = NULL;
            HANDLE stdout_write_child = NULL;

            // Create stdin pipe
            if (!CreatePipe(&stdin_read_child, &stdin_write_parent, &sa, 0))
            {
                error_ = "Failed to create stdin pipe: " + std::to_string(GetLastError());
                return false;
            }
            // Parent's write handle should not be inherited
            SetHandleInformation(stdin_write_parent, HANDLE_FLAG_INHERIT, 0);

            // Create stdout pipe
            if (!CreatePipe(&stdout_read_parent, &stdout_write_child, &sa, 0))
            {
                error_ = "Failed to create stdout pipe: " + std::to_string(GetLastError());
                CloseHandle(stdin_read_child);
                CloseHandle(stdin_write_parent);
                return false;
            }
            // Parent's read handle should not be inherited
            SetHandleInformation(stdout_read_parent, HANDLE_FLAG_INHERIT, 0);

            // Setup STARTUPINFO with redirected handles
            STARTUPINFOA si = {};
            si.cb = sizeof(si);
            si.dwFlags = STARTF_USESTDHANDLES;
            si.hStdInput = stdin_read_child;
            si.hStdOutput = stdout_write_child;
            si.hStdError = GetStdHandle(STD_ERROR_HANDLE); // Inherit stderr

            PROCESS_INFORMATION pi = {};

            // Convert path to absolute
            std::string abs_path = std::filesystem::absolute(executable_path_).string();

            // CreateProcess (command line must be mutable)
            std::string cmdline = "\"" + abs_path + "\"";
            std::vector<char> cmdline_buf(cmdline.begin(), cmdline.end());
            cmdline_buf.push_back('\0');

            BOOL success = CreateProcessA(
                abs_path.c_str(),   // lpApplicationName
                cmdline_buf.data(), // lpCommandLine (mutable)
                NULL,               // lpProcessAttributes
                NULL,               // lpThreadAttributes
                TRUE,               // bInheritHandles
                0,                  // dwCreationFlags
                NULL,               // lpEnvironment
                NULL,               // lpCurrentDirectory
                &si,                // lpStartupInfo
                &pi                 // lpProcessInformation
            );

            // Close child-side handles in parent
            CloseHandle(stdin_read_child);
            CloseHandle(stdout_write_child);

            if (!success)
            {
                error_ = "CreateProcess failed: " + std::to_string(GetLastError());
                CloseHandle(stdin_write_parent);
                CloseHandle(stdout_read_parent);
                return false;
            }

            // Close thread handle (don't need it)
            CloseHandle(pi.hThread);

            // Store handles
            process_handle_ = pi.hProcess;
            stdin_read_ = NULL;   // We closed this (child owns it)
            stdout_write_ = NULL; // We closed this (child owns it)

            // Give to client
            client_.set_handles(stdin_write_parent, stdout_read_parent);

            std::cerr << "[" << provider_id_ << "] Process spawned successfully (PID="
                      << pi.dwProcessId << ")\n";
            return true;
        }
#else
        bool ProviderProcess::spawn_linux()
        {
            // Create pipes
            int stdin_pipe[2];
            int stdout_pipe[2];

            if (pipe(stdin_pipe) < 0)
            {
                error_ = "Failed to create stdin pipe";
                return false;
            }
            if (pipe(stdout_pipe) < 0)
            {
                error_ = "Failed to create stdout pipe";
                close(stdin_pipe[0]);
                close(stdin_pipe[1]);
                return false;
            }

            pid_ = fork();
            if (pid_ < 0)
            {
                error_ = "Fork failed";
                close(stdin_pipe[0]);
                close(stdin_pipe[1]);
                close(stdout_pipe[0]);
                close(stdout_pipe[1]);
                return false;
            }

            if (pid_ == 0)
            {
                // Child process
                // Redirect stdin
                dup2(stdin_pipe[0], STDIN_FILENO);
                close(stdin_pipe[0]);
                close(stdin_pipe[1]);

                // Redirect stdout
                dup2(stdout_pipe[1], STDOUT_FILENO);
                close(stdout_pipe[0]);
                close(stdout_pipe[1]);

                // stderr stays connected to parent's stderr

                // Execute provider
                std::string abs_path = std::filesystem::absolute(executable_path_).string();
                execl(abs_path.c_str(), abs_path.c_str(), nullptr);

                // If we get here, exec failed
                std::cerr << "exec failed for " << abs_path << "\n";
                _exit(1);
            }

            // Parent process
            close(stdin_pipe[0]);  // Close read end of stdin pipe
            close(stdout_pipe[1]); // Close write end of stdout pipe

            stdin_write_fd_ = stdin_pipe[1];
            stdout_read_fd_ = stdout_pipe[0];

            client_.set_handles(stdin_write_fd_, stdout_read_fd_);

            std::cerr << "[" << provider_id_ << "] Process spawned successfully (PID=" << pid_ << ")\n";
            return true;
        }
#endif

        bool ProviderProcess::is_running() const
        {
#ifdef _WIN32
            if (!process_handle_)
                return false;
            DWORD exit_code;
            if (!GetExitCodeProcess(process_handle_, &exit_code))
                return false;
            return exit_code == STILL_ACTIVE;
#else
            if (pid_ <= 0)
                return false;
            int status;
            pid_t result = waitpid(pid_, &status, WNOHANG);
            return result == 0; // 0 means still running
#endif
        }

        void ProviderProcess::shutdown()
        {
            if (!is_running())
                return;

            std::cerr << "[" << provider_id_ << "] Initiating shutdown\n";

            // 1. Send EOF
            client_.close_stdin();

            // 2. Wait with timeout (2 seconds)
            bool exited = wait_for_exit(2000);

            if (exited)
            {
                std::cerr << "[" << provider_id_ << "] Clean shutdown\n";
            }
            else
            {
                // 3. Forced kill
                std::cerr << "[" << provider_id_ << "] Timeout - forcing termination\n";
                force_terminate();
                wait_for_exit(500);
            }

#ifdef _WIN32
            if (process_handle_)
            {
                CloseHandle(process_handle_);
                process_handle_ = nullptr;
            }
#endif
        }

        bool ProviderProcess::wait_for_exit(int timeout_ms)
        {
#ifdef _WIN32
            if (!process_handle_)
                return true;
            DWORD result = WaitForSingleObject(process_handle_, timeout_ms);
            return result == WAIT_OBJECT_0;
#else
            if (pid_ <= 0)
                return true;

            auto start = std::chrono::steady_clock::now();
            while (true)
            {
                int status;
                pid_t result = waitpid(pid_, &status, WNOHANG);
                if (result == pid_)
                {
                    // Process exited
                    pid_ = -1;
                    return true;
                }

                auto elapsed = std::chrono::steady_clock::now() - start;
                if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= timeout_ms)
                {
                    return false;
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
#endif
        }

        void ProviderProcess::force_terminate()
        {
#ifdef _WIN32
            if (process_handle_)
            {
                TerminateProcess(process_handle_, 1);
            }
#else
            if (pid_ > 0)
            {
                kill(pid_, SIGKILL);
            }
#endif
        }

    } // namespace provider
} // namespace anolis
