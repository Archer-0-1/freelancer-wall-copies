#pragma once

#include <Windows.h>

#include <array>
#include <chrono>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string_view>
#include <string>

namespace FreelancerDiagnostics {
inline std::string SessionStartRecord() {
    const auto unixMilliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    return "session-start unix_ms=" + std::to_string(unixMilliseconds) +
        " pid=" + std::to_string(GetCurrentProcessId());
}

inline std::filesystem::path ResolvePluginLogPath() noexcept {
    try {
        HMODULE module = nullptr;
        const auto address = reinterpret_cast<LPCWSTR>(&ResolvePluginLogPath);
        constexpr DWORD flags = GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT;
        if (!GetModuleHandleExW(flags, address, &module))
            return {};

        std::array<wchar_t, 32768> modulePath{};
        const DWORD length = GetModuleFileNameW(module, modulePath.data(),
            static_cast<DWORD>(modulePath.size()));
        if (length == 0 || length >= modulePath.size())
            return {};

        return std::filesystem::path(modulePath.data(), modulePath.data() + length)
            .parent_path() / L"FreelancerOwnedWallCopyAudioRepair.log";
    } catch (...) {
        return {};
    }
}

class BoundedDiagnosticLog {
public:
    bool Open(const std::filesystem::path& path, std::size_t maximumBytes = 65536) noexcept {
        try {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stream.close();
            m_stream.clear();
            m_bytesWritten = 0;
            m_maximumBytes = maximumBytes;
            m_stopped = true;
            if (path.empty() || maximumBytes == 0)
                return false;

            std::error_code error;
            const bool exists = std::filesystem::exists(path, error);
            if (error)
                return false;
            if (exists) {
                const auto existingBytes = std::filesystem::file_size(path, error);
                if (error || existingBytes > maximumBytes)
                    return false;
                m_bytesWritten = static_cast<std::size_t>(existingBytes);
            }
            if (m_bytesWritten >= m_maximumBytes)
                return false;

            m_stream.open(path, std::ios::out | std::ios::binary | std::ios::app);
            if (!m_stream.is_open())
                return false;
            m_stopped = false;
            return true;
        } catch (...) {
            m_stopped = true;
            return false;
        }
    }

    void Write(std::string_view message) noexcept {
        try {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_stopped || !m_stream.is_open())
                return;

            constexpr std::string_view limitMarker = "[log limit reached]\n";
            const auto recordBytes = message.size() + 1;
            if (recordBytes > m_maximumBytes - m_bytesWritten) {
                if (limitMarker.size() <= m_maximumBytes - m_bytesWritten) {
                    try {
                        m_stream.write(limitMarker.data(), static_cast<std::streamsize>(limitMarker.size()));
                        m_stream.flush();
                        if (m_stream)
                            m_bytesWritten += limitMarker.size();
                    } catch (...) {
                    }
                }
                Stop();
                return;
            }

            try {
                m_stream.write(message.data(), static_cast<std::streamsize>(message.size()));
                m_stream.put('\n');
                m_stream.flush();
                if (!m_stream) {
                    Stop();
                    return;
                }
                m_bytesWritten += recordBytes;
            } catch (...) {
                Stop();
            }
        } catch (...) {
        }
    }

    void Close() noexcept {
        try {
            std::lock_guard<std::mutex> lock(m_mutex);
            Stop();
        } catch (...) {
        }
    }

private:
    void Stop() noexcept {
        try {
            if (m_stream.is_open()) {
                m_stream.flush();
                m_stream.close();
            }
        } catch (...) {
        }
        m_stopped = true;
    }

    std::mutex m_mutex;
    std::ofstream m_stream;
    std::size_t m_bytesWritten = 0;
    std::size_t m_maximumBytes = 0;
    bool m_stopped = true;
};
}

