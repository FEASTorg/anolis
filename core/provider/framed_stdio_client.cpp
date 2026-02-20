#include "framed_stdio_client.hpp"

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <errno.h>
#include <poll.h>
#include <unistd.h>
#endif

namespace anolis {
namespace provider {

namespace {
// Invalid handle value for each platform
#ifdef _WIN32
constexpr FramedStdioClient::PipeHandle kInvalidHandle = nullptr;
#else
constexpr FramedStdioClient::PipeHandle kInvalidHandle = -1;
#endif
}  // namespace

FramedStdioClient::FramedStdioClient() : stdin_write_(kInvalidHandle), stdout_read_(kInvalidHandle) {}

FramedStdioClient::~FramedStdioClient() {
    // Note: Handles closed by ProviderProcess, not here
}

void FramedStdioClient::set_handles(PipeHandle stdin_write, PipeHandle stdout_read) {
    stdin_write_ = stdin_write;
    stdout_read_ = stdout_read;
}

bool FramedStdioClient::write_frame(const uint8_t *data, size_t len, int timeout_ms) {
    if (len > kMaxFrameSize) {
        error_ = "Frame too large: " + std::to_string(len) + " bytes";
        return false;
    }

    // Write uint32_le length prefix
    uint32_t len32 = static_cast<uint32_t>(len);
    uint8_t len_buf[4];
    len_buf[0] = (len32 >> 0) & 0xFF;
    len_buf[1] = (len32 >> 8) & 0xFF;
    len_buf[2] = (len32 >> 16) & 0xFF;
    len_buf[3] = (len32 >> 24) & 0xFF;

    // Write length prefix using write_exact
    if (!write_exact(len_buf, 4, timeout_ms)) {
        return false;
    }

    // Write payload using write_exact
    if (len > 0) {
        if (!write_exact(data, len, timeout_ms)) {
            return false;
        }
    }

    return true;
}

bool FramedStdioClient::read_frame(std::vector<uint8_t> &out, int timeout_ms) {
    // Read uint32_le length prefix
    uint8_t len_buf[4];
    if (!read_exact(len_buf, 4, timeout_ms)) {
        if (error_.empty()) {
            error_ = "EOF reading frame length";
        }
        return false;
    }

    uint32_t len = (uint32_t(len_buf[0]) << 0) | (uint32_t(len_buf[1]) << 8) | (uint32_t(len_buf[2]) << 16) |
                   (uint32_t(len_buf[3]) << 24);

    if (len > kMaxFrameSize) {
        error_ = "Frame too large: " + std::to_string(len) + " bytes";
        return false;
    }

    // Read payload
    out.resize(len);
    if (len > 0) {
        if (!read_exact(out.data(), len, timeout_ms)) {
            if (error_.empty()) {
                error_ = "EOF reading frame payload";
            }
            return false;
        }
    }

    return true;
}

bool FramedStdioClient::wait_for_data(int timeout_ms) {
    error_.clear();
#ifdef _WIN32
    auto start = std::chrono::steady_clock::now();
    while (true) {
        DWORD bytes_available = 0;
        if (!PeekNamedPipe(stdout_read_, NULL, 0, NULL, &bytes_available, NULL)) {
            error_ = "PeekNamedPipe failed: " + std::to_string(GetLastError());
            return false;
        }

        if (bytes_available > 0) {
            return true;
        }

        if (timeout_ms <= 0) {
            return false;
        }

        auto elapsed = std::chrono::steady_clock::now() - start;
        if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() >= timeout_ms) {
            return false;
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
#else
    if (stdout_read_ < 0) {
        error_ = "Invalid stdout pipe";
        return false;
    }

    struct pollfd pfd;
    pfd.fd = stdout_read_;
    pfd.events = POLLIN;
    int result = poll(&pfd, 1, timeout_ms);
    if (result < 0) {
        error_ = "poll failed: " + std::string(strerror(errno));
        return false;
    }
    if (result == 0) {
        return false;
    }

    if ((pfd.revents & POLLIN) != 0) {
        return true;
    }
    if ((pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
        error_ = "poll error on stdout pipe";
    }
    return false;
#endif
}

bool FramedStdioClient::write_exact(const uint8_t *buf, size_t n, int timeout_ms) {
    size_t total = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (total < n) {
        // Check timeout if specified
        if (timeout_ms >= 0) {
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
            if (elapsed_ms >= timeout_ms) {
                error_ = "Timeout writing frame";
                return false;
            }
        }

#ifdef _WIN32
        DWORD written = 0;
        if (!WriteFile(stdin_write_, buf + total, static_cast<DWORD>(n - total), &written, NULL)) {
            error_ = "Write failed: " + std::to_string(GetLastError());
            return false;
        }
        if (written == 0) {
            // Unexpected: pipe closed?
            error_ = "Write returned 0 bytes";
            return false;
        }
        total += written;
#else
        ssize_t w = write(stdin_write_, buf + total, n - total);
        if (w < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, retry
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // Would block (shouldn't happen on blocking pipes, but handle defensively)
                // Sleep briefly and retry
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            }
            if (errno == EPIPE) {
                error_ = "Broken pipe (provider terminated)";
            } else {
                error_ = "Write failed: " + std::string(strerror(errno));
            }
            return false;
        }
        if (w == 0) {
            // Unexpected: pipe closed?
            error_ = "Write returned 0 bytes";
            return false;
        }
        total += w;
#endif
    }
    return true;
}

bool FramedStdioClient::read_exact(uint8_t *buf, size_t n, int timeout_ms) {
    size_t total = 0;
    auto start_time = std::chrono::steady_clock::now();

    while (total < n) {
        // If a timeout is specified, wait for data availability
        if (timeout_ms >= 0) {
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();

            if (elapsed_ms >= timeout_ms) {
                if (error_.empty()) {
                    error_ = "Timeout reading frame";
                }
                return false;
            }

            int remaining_ms = static_cast<int>(timeout_ms - elapsed_ms);
            if (!wait_for_data(remaining_ms)) {
                // wait_for_data sets error_ on failure, or just returns false on timeout
                // If it returned false but error_ is empty, it's a timeout.
                if (error_.empty()) {
                    error_ = "Timeout waiting for data chunk";
                }
                return false;
            }
        }

#ifdef _WIN32
        DWORD read = 0;
        // Only read what is available to avoid blocking, or if we trust wait_for_data,
        // we can just call ReadFile. Since wait_for_data ensures > 0 bytes,
        // ReadFile should not block indefinitely if we ask for (n-total).
        // However, on pipes, ReadFile might block if we ask for MORE than available?
        // MSDN says: "If the pipe is a byte-mode pipe... ReadFile returns when one of the following occurs: a write
        // operation completes on the write end of the pipe, the number of bytes requested is read, or the timeout
        // interval elapses." Since this is a value-oriented pipe (simulated), let's be safe. Actually, for anonymous
        // pipes (used here), ReadFile returns as soon as SOME data is available. It does NOT wait for the full buffer
        // unless we use named pipes in message mode (we aren't).

        if (!ReadFile(stdout_read_, buf + total, static_cast<DWORD>(n - total), &read, NULL)) {
            error_ = "Read failed: " + std::to_string(GetLastError());
            return false;
        }
        if (read == 0) {
            // EOF
            return false;
        }
        total += read;
#else
        ssize_t r = read(stdout_read_, buf + total, n - total);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            error_ = "Read failed: " + std::string(strerror(errno));
            return false;
        }
        if (r == 0) {
            // EOF
            return false;
        }
        total += r;
#endif
    }
    return true;
}

void FramedStdioClient::close_stdin() {
#ifdef _WIN32
    if (stdin_write_) {
        CloseHandle(stdin_write_);
        stdin_write_ = nullptr;
    }
#else
    if (stdin_write_ >= 0) {
        close(stdin_write_);
        stdin_write_ = -1;
    }
#endif
}

void FramedStdioClient::close_stdout() {
#ifdef _WIN32
    if (stdout_read_) {
        CloseHandle(stdout_read_);
        stdout_read_ = nullptr;
    }
#else
    if (stdout_read_ >= 0) {
        close(stdout_read_);
        stdout_read_ = -1;
    }
#endif
}

}  // namespace provider
}  // namespace anolis
