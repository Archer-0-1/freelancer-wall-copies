#pragma once

#include <cstddef>
#include <cstdint>

namespace FreelancerActionMappingPolicy {
constexpr size_t kMaximumActions = 4096;

struct Match {
    uintptr_t itemInstance = 0;
    uint64_t repositoryHigh = 0;
    uint64_t repositoryLow = 0;
    bool descriptorPresent = false;
};

enum class Result { NoMatch, Unique, EmptyId, Ambiguous, TooManyActions, TypeRegistryUnavailable };

constexpr bool CanQueryItemInterface(bool typeRegistryAvailable) {
    return typeRegistryAvailable;
}

constexpr bool ExactActionRoute(bool pickupAction, bool itemInterface, bool expectedSpawner,
    bool expectedItemParent, bool itemParentSharesClickedRoot, bool spawnerOwnedByClickedRoot) {
    return pickupAction && itemInterface && expectedSpawner && expectedItemParent &&
        itemParentSharesClickedRoot && spawnerOwnedByClickedRoot;
}

struct Resolution {
    Result result = Result::NoMatch;
    uint64_t repositoryHigh = 0;
    uint64_t repositoryLow = 0;
};

constexpr Resolution Resolve(const Match* matches, size_t count, size_t actionCount) {
    if (actionCount > kMaximumActions)
        return {Result::TooManyActions, 0, 0};
    uintptr_t selectedInstance = 0;
    uint64_t selectedHigh = 0;
    uint64_t selectedLow = 0;
    for (size_t i = 0; i < count; ++i) {
        const auto& match = matches[i];
        if (!match.itemInstance)
            continue;
        if (!match.descriptorPresent || (match.repositoryHigh == 0 && match.repositoryLow == 0))
            return {Result::EmptyId, 0, 0};
        if (!selectedInstance) {
            selectedInstance = match.itemInstance;
            selectedHigh = match.repositoryHigh;
            selectedLow = match.repositoryLow;
            continue;
        }
        // Returned items may have distinct live entities while representing the same
        // repository item. The caller has already constrained every entry to the
        // exact clicked runtime root; identity here is the descriptor GUID.
        if (selectedHigh != match.repositoryHigh || selectedLow != match.repositoryLow)
            return {Result::Ambiguous, 0, 0};
    }
    return selectedInstance ? Resolution{Result::Unique, selectedHigh, selectedLow} :
        Resolution{Result::NoMatch, 0, 0};
}
}
