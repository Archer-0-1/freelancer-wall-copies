#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace FreelancerCopyQueue {
constexpr size_t kCapacity = 4;
constexpr uint64_t kExpiryMs = 5000;
constexpr uint64_t kCooldownMs = 150;
constexpr size_t kSceneCapacity = 256;

struct Request {
    uint64_t rootInstanceToken = 0;
    uint64_t repositoryHigh = 0;
    uint64_t repositoryLow = 0;
    uint64_t enqueuedAtMs = 0;
    char sceneKey[kSceneCapacity]{};
};

enum class EnqueueResult { Added, Duplicate, Cooldown, Full, Invalid };

class Queue {
public:
    EnqueueResult Enqueue(uint64_t rootInstanceToken, uint64_t high, uint64_t low, const char* scene, uint64_t nowMs) {
        if (!rootInstanceToken || !scene || !scene[0] || (high == 0 && low == 0))
            return EnqueueResult::Invalid;
        size_t length = 0;
        while (length < kSceneCapacity && scene[length]) ++length;
        if (length == kSceneCapacity)
            return EnqueueResult::Invalid;
        Expire(nowMs);
        for (size_t i = 0; i < m_size; ++i) {
            if (Same(m_entries[i], rootInstanceToken, high, low, scene))
                return EnqueueResult::Duplicate;
        }
        if (m_hasLast && m_lastRootInstanceToken == rootInstanceToken && m_lastHigh == high && m_lastLow == low &&
            std::strcmp(m_lastScene, scene) == 0 && nowMs >= m_lastAt && nowMs - m_lastAt < kCooldownMs)
            return EnqueueResult::Cooldown;
        if (m_size == kCapacity)
            return EnqueueResult::Full;
        Request& entry = m_entries[m_size++];
        entry.rootInstanceToken = rootInstanceToken;
        entry.repositoryHigh = high;
        entry.repositoryLow = low;
        entry.enqueuedAtMs = nowMs;
        std::memcpy(entry.sceneKey, scene, length + 1);
        return EnqueueResult::Added;
    }

    bool Pop(uint64_t nowMs, Request& result) {
        Expire(nowMs);
        if (!m_size) return false;
        result = m_entries[0];
        for (size_t i = 1; i < m_size; ++i) m_entries[i - 1] = m_entries[i];
        --m_size;
        m_lastRootInstanceToken = result.rootInstanceToken;
        m_lastHigh = result.repositoryHigh;
        m_lastLow = result.repositoryLow;
        m_lastAt = nowMs;
        std::memcpy(m_lastScene, result.sceneKey, kSceneCapacity);
        m_hasLast = true;
        return true;
    }

    void Clear() { m_size = 0; m_hasLast = false; m_lastRootInstanceToken = 0; }
    size_t Size() const { return m_size; }

private:
    static bool Same(const Request& entry, uint64_t rootInstanceToken, uint64_t high, uint64_t low, const char* scene) {
        return entry.rootInstanceToken == rootInstanceToken && entry.repositoryHigh == high && entry.repositoryLow == low &&
            std::strcmp(entry.sceneKey, scene) == 0;
    }
    void Expire(uint64_t nowMs) {
        size_t write = 0;
        for (size_t read = 0; read < m_size; ++read) {
            const auto age = nowMs >= m_entries[read].enqueuedAtMs ? nowMs - m_entries[read].enqueuedAtMs : 0;
            if (age <= kExpiryMs) m_entries[write++] = m_entries[read];
        }
        m_size = write;
        if (m_hasLast && nowMs >= m_lastAt && nowMs - m_lastAt >= kCooldownMs)
            m_hasLast = false;
    }
    std::array<Request, kCapacity> m_entries{};
    size_t m_size = 0;
    uint64_t m_lastRootInstanceToken = 0;
    uint64_t m_lastHigh = 0;
    uint64_t m_lastLow = 0;
    uint64_t m_lastAt = 0;
    char m_lastScene[kSceneCapacity]{};
    bool m_hasLast = false;
};
}
