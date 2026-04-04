#pragma once

/**
 * @file provider_process.hpp
 * @brief Child-process lifecycle wrapper for one provider executable.
 */

#include <memory>
#include <string>
#include <vector>

#include "framed_stdio_client.hpp"

namespace anolis {
namespace provider {

/**
 * @brief Manages the OS process and stdio pipes for one provider instance.
 *
 * ProviderProcess is the low-level lifecycle layer under `ProviderHandle`. It
 * owns process creation, stdio pipe setup, liveness checks, and the shutdown
 * sequence used to end a provider cleanly before escalating to termination.
 *
 * Lifecycle:
 * `spawn()` creates the child and wires its stdio to `FramedStdioClient`.
 * `shutdown()` closes stdin first to signal EOF, waits up to the configured
 * timeout, and then force-terminates the child if needed.
 */
class ProviderProcess {
public:
    /**
     * @brief Construct a provider process wrapper.
     *
     * @param provider_id Runtime-local provider identifier
     * @param executable_path Provider executable path
     * @param args Extra argv entries passed to the child
     * @param shutdown_timeout_ms Grace period before forced termination
     */
    ProviderProcess(const std::string &provider_id, const std::string &executable_path,
                    const std::vector<std::string> &args = {}, int shutdown_timeout_ms = 2000);
    ~ProviderProcess();

    ProviderProcess(const ProviderProcess &) = delete;
    ProviderProcess &operator=(const ProviderProcess &) = delete;

    /**
     * @brief Spawn the provider child process and connect its stdio pipes.
     *
     * @return true on success, false on failure with `last_error()` populated
     */
    bool spawn();

    /** @brief Report whether the child process is still running. */
    bool is_running() const;

    /**
     * @brief Shut down the child using EOF, wait, then force-kill escalation.
     */
    void shutdown();

    /** @brief Access the framed stdio client bound to this child. */
    FramedStdioClient &client() { return client_; }

    /** @brief Return the runtime-local provider identifier. */
    const std::string &provider_id() const { return provider_id_; }

    /** @brief Return the last spawn or lifecycle error message. */
    const std::string &last_error() const { return error_; }

private:
    struct Impl;

    std::string provider_id_;
    std::string executable_path_;
    std::vector<std::string> args_;
    std::string error_;
    int shutdown_timeout_ms_;

    FramedStdioClient client_;
    std::unique_ptr<Impl> impl_;

    /** @brief Platform-specific spawn path for Windows process creation. */
    bool spawn_windows();
    /** @brief Platform-specific spawn path for POSIX fork/exec creation. */
    bool spawn_linux();
    /** @brief Wait for the child to exit within the given timeout. */
    bool wait_for_exit(int timeout_ms);
    /** @brief Forcefully terminate the child process after graceful shutdown fails. */
    void force_terminate();
};

}  // namespace provider
}  // namespace anolis
