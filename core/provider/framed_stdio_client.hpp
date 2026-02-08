#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace anolis {
namespace provider {

// Maximum frame size: 1 MiB (same as provider-sim)
constexpr uint32_t kMaxFrameSize = 1024u * 1024u;

// FramedStdioClient manages length-prefixed binary communication over stdin/stdout
// with a child process. Frames are: uint32_le (length) + payload bytes.
class FramedStdioClient {
public:
    // Platform-specific handle types
#ifdef _WIN32
    using PipeHandle = void *;  // HANDLE
#else
    using PipeHandle = int;  // file descriptor
#endif

    FramedStdioClient();
    ~FramedStdioClient();

    // Delete copy/move (manages OS handles)
    FramedStdioClient(const FramedStdioClient &) = delete;
    FramedStdioClient &operator=(const FramedStdioClient &) = delete;

    // Initialize with pipe handles (stdin_write, stdout_read from parent's perspective)
    void set_handles(PipeHandle stdin_write, PipeHandle stdout_read);

    // Write a length-prefixed frame to provider's stdin
    // Returns true on success, false on error (sets error_)
    bool write_frame(const uint8_t *data, size_t len, int timeout_ms = -1);

    // Read a length-prefixed frame from provider's stdout
    // Returns true on success, false on EOF or error (sets error_)
    bool read_frame(std::vector<uint8_t> &out, int timeout_ms = -1);

    // Wait for data to be available on stdout (non-blocking with timeout)
    // Returns true if data is available, false on timeout or error (sets error_ on error)
    bool wait_for_data(int timeout_ms);

    // Close stdin (signals EOF to provider)
    void close_stdin();

    // Get last error message
    const std::string &last_error() const { return error_; }

private:
    PipeHandle stdin_write_;
    PipeHandle stdout_read_;
    std::string error_;

    // Low-level read exactly n bytes
    bool read_exact(uint8_t *buf, size_t n, int timeout_ms = -1);

    // Low-level write exactly n bytes (handles partial writes, EINTR, etc.)
    bool write_exact(const uint8_t *buf, size_t n, int timeout_ms = -1);
};

}  // namespace provider
}  // namespace anolis
