#include "framed_stdio_client.hpp"
#include <cstring>
#include <iostream>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#include <errno.h>
#endif

namespace anolis
{
    namespace provider
    {

        namespace
        {
// Invalid handle value for each platform
#ifdef _WIN32
            constexpr FramedStdioClient::PipeHandle kInvalidHandle = nullptr;
#else
            constexpr FramedStdioClient::PipeHandle kInvalidHandle = -1;
#endif
        } // namespace

        FramedStdioClient::FramedStdioClient()
            : stdin_write_(kInvalidHandle), stdout_read_(kInvalidHandle) {}

        FramedStdioClient::~FramedStdioClient()
        {
            // Note: Handles closed by ProviderProcess, not here
        }

        void FramedStdioClient::set_handles(PipeHandle stdin_write, PipeHandle stdout_read)
        {
            stdin_write_ = stdin_write;
            stdout_read_ = stdout_read;
        }

        bool FramedStdioClient::write_frame(const uint8_t *data, size_t len)
        {
            if (len > kMaxFrameSize)
            {
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

#ifdef _WIN32
            DWORD written;
            if (!WriteFile(stdin_write_, len_buf, 4, &written, NULL) || written != 4)
            {
                error_ = "Failed to write frame length";
                return false;
            }
            if (len > 0)
            {
                if (!WriteFile(stdin_write_, data, static_cast<DWORD>(len), &written, NULL) ||
                    written != len)
                {
                    error_ = "Failed to write frame payload";
                    return false;
                }
            }
#else
            if (write(stdin_write_, len_buf, 4) != 4)
            {
                error_ = "Failed to write frame length: " + std::string(strerror(errno));
                return false;
            }
            if (len > 0)
            {
                if (write(stdin_write_, data, len) != static_cast<ssize_t>(len))
                {
                    error_ = "Failed to write frame payload: " + std::string(strerror(errno));
                    return false;
                }
            }
#endif

            return true;
        }

        bool FramedStdioClient::read_frame(std::vector<uint8_t> &out)
        {
            // Read uint32_le length prefix
            uint8_t len_buf[4];
            if (!read_exact(len_buf, 4))
            {
                if (error_.empty())
                {
                    error_ = "EOF reading frame length";
                }
                return false;
            }

            uint32_t len = (uint32_t(len_buf[0]) << 0) |
                           (uint32_t(len_buf[1]) << 8) |
                           (uint32_t(len_buf[2]) << 16) |
                           (uint32_t(len_buf[3]) << 24);

            if (len > kMaxFrameSize)
            {
                error_ = "Frame too large: " + std::to_string(len) + " bytes";
                return false;
            }

            // Read payload
            out.resize(len);
            if (len > 0)
            {
                if (!read_exact(out.data(), len))
                {
                    if (error_.empty())
                    {
                        error_ = "EOF reading frame payload";
                    }
                    return false;
                }
            }

            return true;
        }

        bool FramedStdioClient::read_exact(uint8_t *buf, size_t n)
        {
            size_t total = 0;
            while (total < n)
            {
#ifdef _WIN32
                DWORD read;
                if (!ReadFile(stdout_read_, buf + total, static_cast<DWORD>(n - total), &read, NULL))
                {
                    error_ = "Read failed: " + std::to_string(GetLastError());
                    return false;
                }
                if (read == 0)
                {
                    // EOF
                    return false;
                }
                total += read;
#else
                ssize_t r = read(stdout_read_, buf + total, n - total);
                if (r < 0)
                {
                    if (errno == EINTR)
                        continue;
                    error_ = "Read failed: " + std::string(strerror(errno));
                    return false;
                }
                if (r == 0)
                {
                    // EOF
                    return false;
                }
                total += r;
#endif
            }
            return true;
        }

        void FramedStdioClient::close_stdin()
        {
#ifdef _WIN32
            if (stdin_write_)
            {
                CloseHandle(stdin_write_);
                stdin_write_ = nullptr;
            }
#else
            if (stdin_write_ >= 0)
            {
                close(stdin_write_);
                stdin_write_ = -1;
            }
#endif
        }

    } // namespace provider
} // namespace anolis
