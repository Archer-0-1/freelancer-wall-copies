#pragma once

#include <cstdint>
#include <cstddef>

namespace FreelancerFeedbackPolicy {
enum class Status : uint8_t { Hidden, CopyRequested, OriginalTakeRequested, Blocked, AlreadyCarried, Cooldown };
enum class SnapshotSlotChoice : uint8_t { Existing, ReuseFree, Append, Full };

constexpr SnapshotSlotChoice ResolveSnapshotSlot(bool matchingSnapshot, bool freeSlot,
    size_t currentCount, size_t capacity) {
    return matchingSnapshot ? SnapshotSlotChoice::Existing : freeSlot ? SnapshotSlotChoice::ReuseFree :
        currentCount < capacity ? SnapshotSlotChoice::Append : SnapshotSlotChoice::Full;
}

constexpr Status ResolveCopyResult(bool createItemHandle, bool alreadyCarried) {
    return createItemHandle ? Status::CopyRequested :
        (alreadyCarried ? Status::AlreadyCarried : Status::Blocked);
}

constexpr const char* PromptMessageAlias(Status status) {
    switch (status) {
    case Status::CopyRequested: return "WallCopyMessageCopyRequested";
    case Status::OriginalTakeRequested: return "WallCopyMessageOriginalTakeRequested";
    case Status::Blocked: return "WallCopyMessageBlocked";
    case Status::AlreadyCarried: return "WallCopyMessageAlreadyCarried";
    case Status::Cooldown: return "WallCopyMessageCooldown";
    case Status::Hidden: break;
    }
    return "";
}

struct PromptFeedbackRoute {
    Status status;
    bool scenePlaying;
    bool safehouseScene;
    bool sameScene;
    bool rootPresent;
    bool patchMarkerPresent;
    bool hideNotifications;
    uint64_t runtimeRootEntityId;
    uint64_t rootInstanceToken;
    uint64_t expectedRootInstanceToken;
};

constexpr bool CanApplyPromptFeedback(const PromptFeedbackRoute& route) {
    return route.status != Status::Hidden && route.scenePlaying && route.safehouseScene &&
        route.sameScene && route.rootPresent && route.patchMarkerPresent && !route.hideNotifications &&
        route.runtimeRootEntityId != 0 && route.rootInstanceToken != 0 &&
        route.rootInstanceToken == route.expectedRootInstanceToken;
}

constexpr uint64_t ResolvePromptBaseline(bool pending, bool sameRoot, bool sameScene,
    bool currentStillFeedback, uint64_t savedOriginal, uint64_t currentValue) {
    return pending && sameRoot && sameScene && currentStillFeedback ? savedOriginal : currentValue;
}

constexpr bool ShouldAttemptPromptRestore(bool pending, bool expired, bool selectionChanged, bool contextLost) {
    return pending && (expired || selectionChanged || contextLost);
}

constexpr bool CanScanPromptForRestore(bool pending, bool sameScene, bool sameRootSelected,
    bool waitForSelectionCycle) {
    return pending && sameScene && sameRootSelected && !waitForSelectionCycle;
}

constexpr bool CanRestorePromptFeedback(bool scenePlaying, bool safehouseScene, bool sameScene,
    bool rootPresent, bool patchMarkerPresent, bool rootTokenMatches, bool currentStillFeedback) {
    return scenePlaying && safehouseScene && sameScene && rootPresent && patchMarkerPresent &&
        rootTokenMatches && currentStillFeedback;
}
}
