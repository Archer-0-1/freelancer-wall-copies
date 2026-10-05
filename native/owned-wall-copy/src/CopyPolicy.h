#include <cstdint>

#pragma once

namespace FreelancerCopyPolicy {
constexpr uint64_t kRegularGearWallAcceptPrompt = 0x82D0B27F8A85D759ULL;
constexpr uint64_t kLegacyActionXCopyPrompt = 0xAE106D7E11ED863EULL;
constexpr uint64_t kGearWallEntity = 0x76BC7DEBE63916B6ULL;
constexpr uint64_t kMenuEntity = 0xDD218B2743991CA8ULL;
constexpr uint64_t kSelectedLatchEntity = 0x4D8B820D75B03DE6ULL;
constexpr uint64_t kSelectedConditionEntity = 0xD61A2CE5A7B1575AULL;
constexpr uint64_t kMenuSoundEntity = 0x0CD5D1C19B606CB9ULL;

constexpr bool SelectionEventMatches(uint64_t entityId, uint32_t pinId,
    uint32_t onTruePin, uint32_t onFalsePin) {
    return entityId == kSelectedConditionEntity && (pinId == onTruePin || pinId == onFalsePin);
}

constexpr bool PromptEntityMatches(uint64_t entityId) {
    return entityId == kRegularGearWallAcceptPrompt;
}

constexpr bool PromptPinMatches(uint32_t pinId, uint32_t actionTriggeredPin) {
    return pinId == actionTriggeredPin;
}

constexpr bool ExactPromptRoute(uint64_t promptId, uint32_t pinId,
    uint64_t wallId, uint64_t menuId, uint64_t rootId,
    bool wallOwnerIsRoot, bool menuOwnerIsRoot, bool patchMarkerPresent,
    uint32_t actionTriggeredPin) {
    return promptId == kRegularGearWallAcceptPrompt && pinId == actionTriggeredPin &&
        wallId == kGearWallEntity && menuId == kMenuEntity &&
        rootId != 0 && wallOwnerIsRoot && menuOwnerIsRoot && patchMarkerPresent;
}

constexpr bool CanTriggerOriginalTake(bool playing, bool safehouse, bool gameForeground,
    bool inputManagerEnabled, bool unpaused, bool selectedExactWallItem, bool pressEdge,
    bool patchMarkerPresent, bool eligibleNow) {
    return playing && safehouse && gameForeground && inputManagerEnabled && unpaused &&
        selectedExactWallItem && pressEdge && patchMarkerPresent && eligibleNow;
}

enum class PAction { Copy, OriginalTake };
constexpr PAction ResolvePAction(bool defaultKeybinds) {
    return defaultKeybinds ? PAction::Copy : PAction::OriginalTake;
}
constexpr bool CanTriggerPAction(bool playing, bool safehouse, bool gameForeground,
    bool inputManagerEnabled, bool unpaused, bool selectedExactWallItem, bool pressEdge,
    bool patchMarkerPresent, bool eligibleNow) {
    return CanTriggerOriginalTake(playing, safehouse, gameForeground, inputManagerEnabled,
        unpaused, selectedExactWallItem, pressEdge, patchMarkerPresent, eligibleNow);
}

constexpr bool WallInstanceMatches(bool expectedSpawner, bool expectedItemParent,
    bool rootPresent, bool spawnerOwnedByRoot, bool expectedWallReference,
    bool expectedOwnedReference, bool expectedPlacedReference, bool referencesShareRoot) {
    return expectedSpawner && expectedItemParent && rootPresent && spawnerOwnedByRoot &&
        expectedWallReference && expectedOwnedReference && expectedPlacedReference && referencesShareRoot;
}

constexpr bool Eligible(
    bool expectedSpawner,
    bool expectedInstance,
    bool descriptorPresent,
    bool repositoryIdValid,
    bool wallReadable,
    bool wallPlaced,
    bool ownedReadable,
    bool owned,
    bool placedReadable,
    bool placed
) {
    return expectedSpawner && expectedInstance && descriptorPresent && repositoryIdValid && wallReadable && wallPlaced &&
        ownedReadable && owned && placedReadable && placed;
}

constexpr bool SelectionStillEligible(bool repositoryIdMatches, bool currentCandidateEligible) {
    return repositoryIdMatches && currentCandidateEligible;
}

enum class AcceptDisposition { Vanilla, Copy, Blocked };
enum class CarryDisposition { Copy, BlockedAlreadyCarried, Unknown };
constexpr AcceptDisposition ResolveAccept(bool typedPatchAliasPresent, bool exactPromptRoute, bool mappedClickedItem) {
    if (!typedPatchAliasPresent || !exactPromptRoute) return AcceptDisposition::Vanilla;
    return mappedClickedItem ? AcceptDisposition::Copy : AcceptDisposition::Blocked;
}
constexpr AcceptDisposition ResolveConfiguredAccept(bool typedPatchAliasPresent, bool exactPromptRoute,
    bool defaultKeybinds, bool mappedClickedItem) {
    if (!typedPatchAliasPresent || !exactPromptRoute || defaultKeybinds) return AcceptDisposition::Vanilla;
    return mappedClickedItem ? AcceptDisposition::Copy : AcceptDisposition::Blocked;
}

constexpr CarryDisposition ResolveCarry(bool queryReadyReadable, bool queryReady,
    bool carriedReadable, bool carried, bool toolTagKnown, bool isFreelancerTool) {
    if (toolTagKnown && isFreelancerTool) return CarryDisposition::Copy;
    if (!queryReadyReadable || !queryReady || !carriedReadable) return CarryDisposition::Unknown;
    if (!carried) return CarryDisposition::Copy;
    if (!toolTagKnown) return CarryDisposition::Unknown;
    return isFreelancerTool ? CarryDisposition::Copy : CarryDisposition::BlockedAlreadyCarried;
}
}

