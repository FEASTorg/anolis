#pragma once

/**
 * @file framed_stdio_client.hpp
 * @brief Length-prefixed stdio transport used for runtime-to-provider ADPP traffic.
 */

#include <cstdint>
#include <string>
#include <vector>

namespace anolis {
namespace provider {

/** @brief Maximum accepted frame payload size for provider stdio traffic. */
constexpr uint32_t kMaxFrameSize = 1024u * 1024u;

/**
 * @brief Framed binary transport over a provider child process's stdin/stdout.
 *
 * Frames are encoded as `uint32_le length + payload`. The client performs
 * exact reads/writes, enforces `kMaxFrameSize`, and exposes timeout-aware wait,
 * read, and write helpers to `ProviderHandle`.
 *
 * Ownership:
 * OS handles are supplied externally by `ProviderProcess`. This class uses and
 * closes them, but it does not create them.
 */
class FramedStdioClient {
public:
#ifdef _WIN32
    using PipeHandle = void *;  // HANDLE
#else
    using PipeHandle = int;  // file descriptor
#endif

    FramedStdioClient();
    ~FramedStdioClient();

    FramedStdioClient(const FramedStdioClient &) = delete;
    FramedStdioClient &operator=(const FramedStdioClient &) = delete;

    /** @brief Bind the client to already-created parent-side pipe handles. */
    void set_handles(PipeHandle stdin_write, PipeHandle stdout_read);

    /** @brief Write one complete length-prefixed frame to the provider. */
    bool write_frame(const uint8_t *data, size_t len, int timeout_ms = -1);

    /** @brief Read one complete length-prefixed frame from the provider. */
    bool read_frame(std::vector<uint8_t> &out, int timeout_ms = -1);

    /** @brief Wait for provider stdout to become readable within a timeout. */
    bool wait_for_data(int timeout_ms);

    /** @brief Close the provider stdin pipe to signal EOF. */
    void close_stdin();

    /** @brief Close the provider stdout read handle. */
    void close_stdout();

    /** @brief Return the last transport-layer error message. */
    const std::string &last_error() const { return error_; }

private:
    PipeHandle stdin_write_;
    PipeHandle stdout_read_;
    std::string error_;

    /** @brief Read exactly `n` bytes or fail with EOF/error/timeout. */
    bool read_exact(uint8_t *buf, size_t n, int timeout_ms = -1);

    /** @brief Write exactly `n` bytes, handling partial writes and interruptions. */
    bool write_exact(const uint8_t *buf, size_t n, int timeout_ms = -1);
};

}  // namespace provider
}  // namespace anolis
