#pragma once

#include <algorithm>
#include <cstdint>

namespace FreelancerFeedbackPresentation {
constexpr uint64_t kPreviewDurationMs = 12000;

struct Animation {
    bool visible = false;
    float opacity = 0.0f;
    float slideOffset = 0.0f;
    float remaining = 0.0f;
};

struct Layout {
    float scale = 1.0f;
    float toastX = 0.0f;
    float toastY = 0.0f;
    float toastWidth = 0.0f;
    float toastHeight = 0.0f;
    float hintX = 0.0f;
    float hintY = 0.0f;
    float hintWidth = 0.0f;
    float hintHeight = 0.0f;
};

constexpr float Clamp(float value, float low, float high) {
    return value < low ? low : value > high ? high : value;
}

constexpr float EaseOut(float value) {
    const float t = Clamp(value, 0.0f, 1.0f);
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse;
}

constexpr float GameplayHintOpacity(bool selected, const Animation&) {
    return selected ? 1.0f : 0.0f;
}

constexpr Animation MakeAnimation(uint64_t nowMs, uint64_t changedAtMs, uint64_t durationMs,
    uint64_t enterMs = 180, uint64_t exitMs = 300) {
    if (!durationMs || nowMs < changedAtMs)
        return {};
    const uint64_t ageMs = nowMs - changedAtMs;
    if (ageMs >= durationMs)
        return {};

    const float enter = EaseOut(enterMs ? static_cast<float>(ageMs) / static_cast<float>(enterMs) : 1.0f);
    const float exit = EaseOut(exitMs ? static_cast<float>(durationMs - ageMs) / static_cast<float>(exitMs) : 1.0f);
    return {true, std::min(enter, exit), 16.0f * (1.0f - enter) + 6.0f * (1.0f - exit),
        1.0f - static_cast<float>(ageMs) / static_cast<float>(durationMs)};
}

constexpr Layout MakeLayout(float viewX, float viewY, float viewWidth, float viewHeight) {
    const float safeWidth = std::max(0.0f, viewWidth);
    const float safeHeight = std::max(0.0f, viewHeight);
    const float scale = Clamp(safeHeight / 1080.0f, 0.72f, 1.45f);
    const float insetX = std::max(20.0f * scale, safeWidth * 0.025f);
    const float topInset = std::max(64.0f * scale, safeHeight * 0.05f);
    const float bottomInset = std::max(28.0f * scale, safeHeight * 0.04f);
    const float toastWidth = std::min(480.0f * scale, std::max(0.0f, safeWidth - 2.0f * insetX));
    const float toastHeight = 94.0f * scale;
    const float hintWidth = std::min(430.0f * scale, std::max(0.0f, safeWidth - 2.0f * insetX));
    const float hintHeight = 38.0f * scale;
    return {scale,
        viewX + safeWidth - insetX - toastWidth,
        viewY + topInset,
        toastWidth,
        toastHeight,
        viewX + (safeWidth - hintWidth) * 0.5f,
        viewY + safeHeight - bottomInset - hintHeight,
        hintWidth,
        hintHeight};
}
}
