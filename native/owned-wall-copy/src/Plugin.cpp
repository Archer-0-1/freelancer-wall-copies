#include <IPluginInterface.h>
#include <Functions.h>
#include <Glacier/ZAction.h>
#include <Glacier/ZEntity.h>
#include <Glacier/EntityFactory.h>
#include <Glacier/ZHitman5.h>
#include <Glacier/ZGameLoopManager.h>
#include <Glacier/ZInventory.h>
#include <Glacier/ZItem.h>
#include <Glacier/ZModule.h>
#include <Glacier/ZResourceID.h>
#include <Glacier/ZString.h>
#include <Glacier/ZEntityManager.h>
#include <Glacier/ZInputActionManager.h>
#include <Glacier/ZGameTime.h>
#include <Logging.h>

#include "CopyPolicy.h"
#include "ActionMappingPolicy.h"
#include "CopyRequestQueue.h"
#include "DiagnosticLog.h"
#include "TraversalPolicy.h"
#include "FeedbackPolicy.h"
#include "FeedbackPresentation.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <atomic>
#include <chrono>
#include <string>
#include <string_view>
#include <vector>
#include <Windows.h>

namespace {
constexpr uint64_t kWallSpawnerEntityId = 0xB77691E22DD91FC8ULL;
constexpr uint64_t kItemParentEntityId = 0xFF28C16EB9073F6EULL;
constexpr uint64_t kGearWallChildId = 0x13C9381EE813044DULL;
constexpr uint64_t kOwnedChildId = 0x64FBE690FC534C94ULL;
constexpr uint64_t kPlacedChildId = 0x15FBF45986917512ULL;
constexpr uint32_t kValueBoolPropertyId = Hash::Crc32("m_bValue");

enum class BoolReadResult {
    MissingEntity,
    MissingProperty,
    WrongType,
    Unreadable,
    FalseValue,
    TrueValue
};

struct Candidate {
    ZRepositoryID repositoryId;
    uint64_t rootInstanceToken = 0;
    ZEntityRef rootEntity;
    std::string title;
};

struct ScanReport {
    std::vector<Candidate> candidates;
    bool sceneReady = false;
    bool playerReady = false;
    bool actionManagerAvailable = false;
    bool typeRegistryAvailable = false;
    bool memoryManagerAvailable = false;
    int loadingStage = -1;
    bool scenePlaying = false;
    bool safehouseScene = false;
    bool actionLimitReached = false;
    size_t actions = 0;
    size_t matchedWallSpawners = 0;
    size_t wrongHelperInstances = 0;
    size_t nonPickupActions = 0;
    size_t nonItems = 0;
    size_t duplicateItems = 0;
    size_t missingDescriptors = 0;
    size_t emptyRepositoryIds = 0;
    size_t missingSpawners = 0;
    size_t wrongSpawnerTypes = 0;
    size_t missingWallChildren = 0;
    size_t missingOwnedChildren = 0;
    size_t missingPlacedChildren = 0;
    size_t unknownWall = 0;
    size_t falseWall = 0;
    size_t unknownOwned = 0;
    size_t falseOwned = 0;
    size_t unknownPlaced = 0;
    size_t falsePlaced = 0;
    uint64_t lastSpawnerEntityId = 0;
    uint64_t lastItemParentEntityId = 0;
    uint64_t lastHelperRootEntityId = 0;
};

const char* ScanTriggerName(FreelancerTreePolicy::ScanTrigger trigger) {
    switch (trigger) {
    case FreelancerTreePolicy::ScanTrigger::CopyClick: return "copy";
    }
    return "unknown";
}

const char* SceneStageName(int stage) {
    switch (static_cast<ESceneLoadingStage>(stage)) {
    case ESceneLoadingStage::eLoading_Start: return "start";
    case ESceneLoadingStage::eLoading_SceneStopped: return "stopped";
    case ESceneLoadingStage::eLoading_SceneDeleted: return "deleted";
    case ESceneLoadingStage::eLoading_AssetsLoaded: return "assets-loaded";
    case ESceneLoadingStage::eLoading_SceneAllocated: return "scene-allocated";
    case ESceneLoadingStage::eLoading_SceneStarted: return "scene-started";
    case ESceneLoadingStage::eLoading_ScenePrecaching: return "precaching";
    case ESceneLoadingStage::eLoading_SceneActivated: return "activated";
    case ESceneLoadingStage::eLoading_ScenePlaying: return "playing";
    case ESceneLoadingStage::eLoading_MAX: return "max";
    }
    return "unknown";
}



std::string HexId(uint64_t id) {
    char buffer[17]{};
    std::snprintf(buffer, sizeof(buffer), "%016llX", static_cast<unsigned long long>(id));
    return buffer;
}

void LogScanIfChanged(const ScanReport& report) {
    Logger::Info("FWCopy local scan stage={} safehouse={} actions={} matched={} wrongInstance={} missingRefs={}/{}/{} unknownFlags={}/{}/{} falseFlags={}/{}/{} eligible={}",
        report.loadingStage, report.safehouseScene, report.actions, report.matchedWallSpawners,
        report.wrongHelperInstances, report.missingWallChildren, report.missingOwnedChildren,
        report.missingPlacedChildren, report.unknownWall, report.unknownOwned, report.unknownPlaced,
        report.falseWall, report.falseOwned, report.falsePlaced, report.candidates.size());
}

BoolReadResult ReadBoolProperty(const ZEntityRef& entity) {
    if (!entity || !entity->GetType())
        return BoolReadResult::MissingEntity;

    const auto* property = entity->GetType()->FindProperty(kValueBoolPropertyId);
    if (!property || !property->m_pPropertyInfo)
        return BoolReadResult::MissingProperty;

    const auto* namedProperty = property->GetPropertyInfo();
    if (!namedProperty || !namedProperty->m_propertyInfo.m_Type)
        return BoolReadResult::MissingProperty;

    const auto* valueType = namedProperty->m_propertyInfo.m_Type->GetTypeInfo();
    if (!valueType || !valueType->pszTypeName ||
        Hash::Crc32(valueType->pszTypeName) != ZHMTypeId<bool>) {
        return BoolReadResult::WrongType;
    }
    if (!Globals::MemoryManager || !*Globals::MemoryManager)
        return BoolReadResult::Unreadable;

    auto value = entity.GetProperty<bool>(kValueBoolPropertyId);
    const auto* boolValue = value.As<bool>();
    if (!boolValue)
        return BoolReadResult::Unreadable;

    return *boolValue ? BoolReadResult::TrueValue : BoolReadResult::FalseValue;
}

enum class AliasBoolResult { Missing, WrongType, Unreadable, FalseValue, TrueValue };

AliasBoolResult ReadTypedBoolAlias(const ZEntityRef& entity, const char* alias) {
    if (!entity || !entity->GetType() || !alias || !Globals::MemoryManager || !*Globals::MemoryManager)
        return AliasBoolResult::Unreadable;
    const uint32_t propertyId = Hash::Crc32(alias);
    const auto* property = entity->GetType()->FindProperty(propertyId);
    if (!property || !property->m_pPropertyInfo) return AliasBoolResult::Missing;
    const auto* named = property->GetPropertyInfo();
    const auto* type = named && named->m_propertyInfo.m_Type ? named->m_propertyInfo.m_Type->GetTypeInfo() : nullptr;
    if (!type || !type->pszTypeName || Hash::Crc32(type->pszTypeName) != ZHMTypeId<bool> ||
        type->m_nTypeSize != sizeof(bool)) return AliasBoolResult::WrongType;
    const auto value = entity.GetProperty<bool>(propertyId);
    const auto* boolean = value.As<bool>();
    if (!boolean) return AliasBoolResult::Unreadable;
    return *boolean ? AliasBoolResult::TrueValue : AliasBoolResult::FalseValue;
}

uint64_t EntityId(const ZEntityRef& entity) {
    const auto* type = entity ? entity->GetType() : nullptr;
    return type ? type->m_nEntityID : 0;
}

uint64_t RuntimeInstanceToken(const ZEntityRef& entity) {
    const auto* instance = entity.GetEntity();
    // This opaque scalar uses the exact identity that ZEntityRef::operator== compares.
    // It is never cast back or dereferenced, and the live entity ref stays within this scan/update.
    return instance ? static_cast<uint64_t>(reinterpret_cast<uintptr_t>(instance)) : 0;
}

ZEntityRef ReadConditionAlias(const ZEntityRef& entity, const char* alias) {
    const auto* type = entity ? entity->GetType() : nullptr;
    const auto* property = type ? type->FindProperty(Hash::Crc32(alias)) : nullptr;
    if (!property || !property->m_pPropertyInfo)
        return {};
    const auto* info = property->GetPropertyInfo();
    const auto* valueType = info && info->m_propertyInfo.m_Type ?
        info->m_propertyInfo.m_Type->GetTypeInfo() : nullptr;
    // The live v4.1.1 helper aliases are TEntityRef<IBoolCondition>, not ZEntityRef.
    if (!valueType || !valueType->pszTypeName ||
        Hash::Crc32(valueType->pszTypeName) != ZHMTypeId<TEntityRef<IBoolCondition>> ||
        valueType->m_nTypeSize != sizeof(TEntityRef<IBoolCondition>)) {
        return {};
    }
    auto value = entity.GetProperty<TEntityRef<IBoolCondition>>(Hash::Crc32(alias));
    const auto* reference = value.As<TEntityRef<IBoolCondition>>();
    return reference && *reference ? reference->m_entityRef : ZEntityRef{};
}

bool ResolveWallAndFlags(const ZEntityRef& spawner, ScanReport& report,
    ZEntityRef& wall, ZEntityRef& owned, ZEntityRef& placed) {
    const auto parent = spawner.GetLogicalParent();
    report.lastItemParentEntityId = EntityId(parent);
    if (report.lastItemParentEntityId != kItemParentEntityId) {
        ++report.wrongHelperInstances;
        return false;
    }
    const auto root = parent.GetLogicalParent();
    report.lastHelperRootEntityId = EntityId(root);
    wall = ReadConditionAlias(parent, "GearWall");
    owned = ReadConditionAlias(parent, "Owned");
    placed = ReadConditionAlias(parent, "Placed");
    if (!wall) { ++report.missingWallChildren; return false; }
    if (!owned) { ++report.missingOwnedChildren; return false; }
    if (!placed) { ++report.missingPlacedChildren; return false; }
    const bool valid = FreelancerCopyPolicy::WallInstanceMatches(
        EntityId(spawner) == kWallSpawnerEntityId,
        EntityId(parent) == kItemParentEntityId,
        static_cast<bool>(root),
        spawner.GetOwningEntity() == root,
        EntityId(wall) == kGearWallChildId,
        EntityId(owned) == kOwnedChildId,
        EntityId(placed) == kPlacedChildId,
        wall.GetLogicalParent() == root && owned.GetLogicalParent() == root &&
            placed.GetLogicalParent() == root);
    if (!valid)
        ++report.wrongHelperInstances;
    return valid;
}

ScanReport ScanOwnedWallItems(
    FreelancerTreePolicy::ScanTrigger trigger,
    FreelancerDiagnostics::BoundedDiagnosticLog& diagnostics
) {
    ScanReport report;
    const auto finish = [&report, &diagnostics, trigger]() {
        diagnostics.Write("scan-finish route=local-alias trigger=" + std::string(ScanTriggerName(trigger)) +
            " stage=" + std::to_string(report.loadingStage) +
            " safehouse=" + std::to_string(report.safehouseScene) +
            " actions=" + std::to_string(report.actions) +
            " matched=" + std::to_string(report.matchedWallSpawners) +
            " wrong_instance=" + std::to_string(report.wrongHelperInstances) +
            " missing_refs=" + std::to_string(report.missingWallChildren) + "/" +
                std::to_string(report.missingOwnedChildren) + "/" + std::to_string(report.missingPlacedChildren) +
            " unknown_flags=" + std::to_string(report.unknownWall) + "/" +
                std::to_string(report.unknownOwned) + "/" + std::to_string(report.unknownPlaced) +
            " false_flags=" + std::to_string(report.falseWall) + "/" +
                std::to_string(report.falseOwned) + "/" + std::to_string(report.falsePlaced) +
            " action_limit=" + std::to_string(report.actionLimitReached) +
            " eligible=" + std::to_string(report.candidates.size()));

        LogScanIfChanged(report);
        return report;
    };
    diagnostics.Write("scan-enter trigger=" + std::string(ScanTriggerName(trigger)));
    auto* module = Globals::Hitman5Module;
    if (!module || !module->m_pEntitySceneContext)
        return finish();
    auto* sceneContext = module->m_pEntitySceneContext;
    report.loadingStage = static_cast<int>(sceneContext->m_LoadingStage);
    report.scenePlaying = report.loadingStage == static_cast<int>(ESceneLoadingStage::eLoading_ScenePlaying);
    diagnostics.Write("scan-stage trigger=" + std::string(ScanTriggerName(trigger)) +
        " value=" + std::to_string(report.loadingStage) + " name=" + SceneStageName(report.loadingStage));
    if (!FreelancerTreePolicy::ShouldTraverse(trigger, report.scenePlaying)) {
        diagnostics.Write("scan-suppressed trigger=" + std::string(ScanTriggerName(trigger)) +
            " stage=" + std::to_string(report.loadingStage));
        return finish();
    }
    if (!module->IsEngineInitialized())
        return finish();
    const auto& sceneResource = sceneContext->m_SceneInitParameters.m_SceneResource;
    const std::string sceneKey(sceneResource.c_str(), sceneResource.size());
    report.safehouseScene = sceneKey.starts_with("assembly:/_pro/scenes/missions/snug/");
    if (!report.safehouseScene)
        return finish();
    report.sceneReady = true;

    auto* sdk = SDK();
    if (!sdk)
        return finish();
    const auto player = sdk->GetLocalPlayer();
    if (!player || !player.m_pInterfaceRef || !player.m_pInterfaceRef->m_pCharacter)
        return finish();
    report.playerReady = true;

    auto* manager = Globals::HM5ActionManager;
    if (!manager)
        return finish();
    report.actionManagerAvailable = true;
    if (!Globals::TypeRegistry || !*Globals::TypeRegistry)
        return finish();
    report.typeRegistryAvailable = true;
    if (!Globals::MemoryManager || !*Globals::MemoryManager)
        return finish();
    report.memoryManagerAvailable = true;

    if (!sceneContext || !sceneContext->m_pScene || !sceneContext->m_pScene.m_entityRef ||
        sceneContext->m_aLoadedBricks.size() == 0) {
        return finish();
    }

    report.actions = manager->m_Actions.size();
    constexpr size_t kMaximumActions = 4096;
    if (report.actions > kMaximumActions) {
        report.actionLimitReached = true;
        return finish();
    }
    diagnostics.Write("scan-actions-start trigger=" + std::string(ScanTriggerName(trigger)) +
        " stage=" + std::to_string(report.loadingStage) + " actions=" + std::to_string(report.actions));
    for (size_t i = 0; i < report.actions; ++i) {
        if (i != 0 && i % 256 == 0) {
            diagnostics.Write("scan-actions-progress trigger=" + std::string(ScanTriggerName(trigger)) +
                " stage=" + std::to_string(report.loadingStage) + " actions_seen=" + std::to_string(i));
        }
        auto* action = manager->m_Actions[i];
        if (!action) {
            ++report.nonItems;
            continue;
        }
        if (action->m_eActionType != EActionType::AT_PICKUP) {
            ++report.nonPickupActions;
            continue;
        }
        if (!action->m_Object) {
            ++report.nonItems;
            continue;
        }

        auto* actionObject = action->m_Object.GetEntity();
        auto* actionType = actionObject ? actionObject->GetType() : nullptr;
        if (!actionType || !actionType->m_pInterfaceData) {
            ++report.nonItems;
            continue;
        }
        auto* item = action->m_Object.QueryInterface<ZHM5Item>();
        if (!item) {
            ++report.nonItems;
            continue;
        }
        if (!item->m_pItemConfigDescriptor) {
            ++report.missingDescriptors;
            continue;
        }
        if (item->m_pItemConfigDescriptor->m_ItemID.IsEmpty()) {
            ++report.emptyRepositoryIds;
            continue;
        }
        if (!item->m_rSpawner) {
            ++report.missingSpawners;
            continue;
        }

        const auto* spawnerEntity = item->m_rSpawner.GetEntity();
        const auto* spawnerType = spawnerEntity ? spawnerEntity->GetType() : nullptr;
        const uint64_t spawnerId = spawnerType ? spawnerType->m_nEntityID : 0;
        report.lastSpawnerEntityId = spawnerId;
        if (spawnerId != kWallSpawnerEntityId) {
            ++report.wrongSpawnerTypes;
            continue;
        }

        ZEntityRef wall;
        ZEntityRef owned;
        ZEntityRef placed;
        const bool expectedInstance = ResolveWallAndFlags(item->m_rSpawner, report, wall, owned, placed);
        if (!expectedInstance)
            continue;
        ++report.matchedWallSpawners;

        const auto wallValue = ReadBoolProperty(wall);
        if (wallValue != BoolReadResult::TrueValue && wallValue != BoolReadResult::FalseValue) {
            ++report.unknownWall;
            continue;
        }
        if (wallValue == BoolReadResult::FalseValue) {
            ++report.falseWall;
            continue;
        }

        const auto ownedValue = ReadBoolProperty(owned);
        if (ownedValue != BoolReadResult::TrueValue && ownedValue != BoolReadResult::FalseValue) {
            ++report.unknownOwned;
            continue;
        }
        if (ownedValue == BoolReadResult::FalseValue) {
            ++report.falseOwned;
            continue;
        }

        const auto placedValue = ReadBoolProperty(placed);
        if (placedValue != BoolReadResult::TrueValue && placedValue != BoolReadResult::FalseValue) {
            ++report.unknownPlaced;
            continue;
        }
        if (placedValue == BoolReadResult::FalseValue) {
            ++report.falsePlaced;
            continue;
        }

        const bool ownedKnown = ownedValue == BoolReadResult::TrueValue || ownedValue == BoolReadResult::FalseValue;
        const bool placedKnown = placedValue == BoolReadResult::TrueValue || placedValue == BoolReadResult::FalseValue;
        if (!FreelancerCopyPolicy::Eligible(
                spawnerId == kWallSpawnerEntityId,
                expectedInstance,
                item->m_pItemConfigDescriptor != nullptr,
                !item->m_pItemConfigDescriptor->m_ItemID.IsEmpty(),
                wallValue == BoolReadResult::TrueValue || wallValue == BoolReadResult::FalseValue,
                wallValue == BoolReadResult::TrueValue,
                ownedKnown,
                ownedValue == BoolReadResult::TrueValue,
                placedKnown,
                placedValue == BoolReadResult::TrueValue)) {
            continue;
        }

        Candidate candidate;
        candidate.repositoryId = item->m_pItemConfigDescriptor->m_ItemID;
        candidate.rootEntity = item->m_rSpawner.GetLogicalParent().GetLogicalParent();
        candidate.rootInstanceToken = RuntimeInstanceToken(candidate.rootEntity);
        if (std::any_of(report.candidates.begin(), report.candidates.end(),
            [&candidate](const Candidate& current) { return current.rootInstanceToken == candidate.rootInstanceToken &&
                current.repositoryId == candidate.repositoryId; })) {
            ++report.duplicateItems;
            continue;
        }
        const char* title = item->m_pItemConfigDescriptor->m_sTitle.c_str();
        candidate.title = title ? title : "(untitled item)";
        report.candidates.push_back(std::move(candidate));
    }

    return finish();
}

bool CopyCurrentEligibleItem(
    const ZRepositoryID& repositoryId,
    uint64_t rootInstanceToken,
    ZEntityRef& notificationRoot,
    std::string& result,
    FreelancerCopyPolicy::CarryDisposition& carryDisposition,
    FreelancerDiagnostics::BoundedDiagnosticLog& diagnostics
) {
    const auto finish = [&repositoryId, &result, &diagnostics](bool success) {
        const auto id = repositoryId.ToString(ZGuid::GuidFormat::NoDashes);
        Logger::Info("FWCopy copy repo={} success={} outcome={}", id.c_str(), success, result);
        diagnostics.Write("copy-finish repo=" + std::string(id.c_str()) +
            " success=" + std::to_string(success) + " outcome=" + result);
        return success;
    };
    // Rescan at click time and retain no engine pointer between frames.
    diagnostics.Write("copy-click repo=" + std::string(repositoryId.ToString(ZGuid::GuidFormat::NoDashes).c_str()));
    const auto current = ScanOwnedWallItems(FreelancerTreePolicy::ScanTrigger::CopyClick, diagnostics);
    if (!current.scenePlaying) {
        result = "Wait until the safehouse has finished loading, then refresh the wall list.";
        return finish(false);
    }
    const auto selected = std::find_if(current.candidates.begin(), current.candidates.end(),
        [&repositoryId, rootInstanceToken](const Candidate& candidate) {
            return candidate.rootInstanceToken == rootInstanceToken &&
                FreelancerCopyPolicy::SelectionStillEligible(candidate.repositoryId == repositoryId, true);
        });
    if (selected == current.candidates.end()) {
        result = "The item is no longer confirmed as owned and placed on this safehouse wall.";
        return finish(false);
    }
    notificationRoot = selected->rootEntity;

    const auto toolboxValue = ReadTypedBoolAlias(selected->rootEntity, "Audio_IsToolboxItem");
    const bool toolboxTagKnown = toolboxValue == AliasBoolResult::TrueValue ||
        toolboxValue == AliasBoolResult::FalseValue;
    const bool isToolboxItem = toolboxValue == AliasBoolResult::TrueValue;
    AliasBoolResult queryReady = AliasBoolResult::Missing;
    AliasBoolResult carriedValue = AliasBoolResult::Missing;
    if (!isToolboxItem) {
        selected->rootEntity.SignalInputPin(Hash::Crc32("WallCopyResetCarried"));
        selected->rootEntity.SignalInputPin(Hash::Crc32("WallCopyCheckCarried"));
        queryReady = ReadTypedBoolAlias(selected->rootEntity, "WallCopyCarryQueryReady");
        carriedValue = ReadTypedBoolAlias(selected->rootEntity, "WallCopyCarried");
    }
    const bool queryReadyReadable = queryReady == AliasBoolResult::TrueValue ||
        queryReady == AliasBoolResult::FalseValue;
    const bool queryIsReady = queryReady == AliasBoolResult::TrueValue;
    const bool carriedReadable = carriedValue == AliasBoolResult::TrueValue ||
        carriedValue == AliasBoolResult::FalseValue;
    const bool itemIsCarried = carriedValue == AliasBoolResult::TrueValue;
    carryDisposition = FreelancerCopyPolicy::ResolveCarry(queryReadyReadable, queryIsReady,
        carriedReadable, itemIsCarried, toolboxTagKnown, isToolboxItem);
    if (carryDisposition != FreelancerCopyPolicy::CarryDisposition::Copy) {
        if (carryDisposition == FreelancerCopyPolicy::CarryDisposition::BlockedAlreadyCarried)
            result = "This item is already carried.";
        else
            result = "Could not confirm the game's carried-item check.";
        diagnostics.Write("copy-blocked carry_query_ready=" + std::to_string(queryIsReady) +
            " carried=" + (carriedReadable ? std::to_string(itemIsCarried) : std::string("unreadable")) +
            " toolbox_tag=" + (toolboxTagKnown ? std::to_string(isToolboxItem) : std::string("unknown")));
        return finish(false);
    }
    diagnostics.Write(std::string("copy-carry-check toolbox=") + (isToolboxItem ? "1" : "0") +
        " ready=" + (queryReadyReadable ? std::to_string(queryIsReady) : std::string("not-needed")) +
        " carried=" + (carriedReadable ? std::to_string(itemIsCarried) : std::string("not-needed")));

    auto* sdk = SDK();
    if (!sdk) {
        result = "SDK interface is unavailable.";
        return finish(false);
    }
    if (!Globals::TypeRegistry || !*Globals::TypeRegistry) {
        result = "The game type registry is unavailable.";
        return finish(false);
    }
    const auto player = sdk->GetLocalPlayer();
    if (!player || !player.m_pInterfaceRef || !player.m_pInterfaceRef->m_pCharacter) {
        result = "The local player or character is unavailable.";
        return finish(false);
    }

    const auto character = player.m_pInterfaceRef->m_pCharacter;
    const auto container = character.m_pInterfaceRef->m_rSubcontrollerContainer;
    if (!container || !container.m_pInterfaceRef || container.m_pInterfaceRef->m_aReferencedControllers.size() == 0) {
        result = "The character has no subcontroller list.";
        return finish(false);
    }

    ZCharacterSubcontrollerInventory* inventory = nullptr;
    size_t inventoryControllers = 0;
    for (size_t i = 0; i < container.m_pInterfaceRef->m_aReferencedControllers.size(); ++i) {
        const auto controllerEntity = container.m_pInterfaceRef->m_aReferencedControllers[i].m_entityRef;
        if (!controllerEntity)
            continue;
        const auto* controllerType = controllerEntity->GetType();
        if (!controllerType || !controllerType->m_pInterfaceData)
            continue;
        if (auto* currentInventory = controllerEntity.QueryInterface<ZCharacterSubcontrollerInventory>()) {
            inventory = currentInventory;
            ++inventoryControllers;
        }
    }
    if (inventoryControllers != 1 || !inventory) {
        result = "Expected one inventory controller; found " + std::to_string(inventoryControllers) + ".";
        return finish(false);
    }
    if (!Functions::ZCharacterSubcontrollerInventory_CreateItem ||
        !Functions::ZCharacterSubcontrollerInventory_CreateItem->Exists()) {
        result = "The SDK inventory CreateItem function is unavailable.";
        return finish(false);
    }

    const auto id = repositoryId.ToString(ZGuid::GuidFormat::NoDashes);
    diagnostics.Write("create-item-before repo=" + std::string(id.c_str()) +
        " stage=" + std::to_string(current.loadingStage));
    TArray<ZRepositoryID> modifiers;
    const auto created = Functions::ZCharacterSubcontrollerInventory_CreateItem->Call(
        inventory,
        repositoryId,
        ZString(),
        modifiers,
        ZCharacterSubcontrollerInventory::ECreateItemType::ECIT_ContractItem
    );
    diagnostics.Write("create-item-after repo=" + std::string(id.c_str()) +
        " handle=" + std::to_string(static_cast<bool>(created)));
    if (!created) {
        result = "No creation request handle returned; check your inventory.";
        return finish(false);
    }

    result = "Copy requested. Check your inventory and the original on the wall.";
    return finish(true);
}

class FreelancerOwnedWallCopy final : public IPluginInterface {
public:
    void Init() override {
        const char* runtimeVersion = SDKVersion();
        m_diagnostics.Open(FreelancerDiagnostics::ResolvePluginLogPath());
        m_diagnostics.Write(FreelancerDiagnostics::SessionStartRecord());
        m_diagnostics.Write(std::string("plugin-init compiled_sdk=") + ZHMMODSDK_VER +
            " runtime_sdk=" + std::string(runtimeVersion ? runtimeVersion : "unknown"));
        RegisterSignalHook();
        RegisterUpdateCallback();
    }

    void OnEngineInitialized() override {
        RegisterSignalHook();
        RegisterUpdateCallback();
    }

    void OnDrawUI(bool) override {
        if (!ImGui::GetCurrentContext()) return;
        if (!m_sdkRenderCallbackLogged) {
            m_sdkRenderCallbackLogged = true;
            m_diagnostics.Write("sdk-popup-render-ready");
        }
        const auto feedback = ReadSdkFeedback();
        if (feedback.status == FreelancerFeedbackPolicy::Status::Hidden) return;
        const auto animation = FreelancerFeedbackPresentation::MakeAnimation(
            NowMs(), feedback.changedAtMs, kSdkFeedbackDurationMs);
        if (!animation.visible || animation.opacity <= 0.01f) return;
        const auto* viewport = ImGui::GetMainViewport();
        if (!viewport || viewport->Size.x <= 0.0f || viewport->Size.y <= 0.0f) return;
        const auto layout = FreelancerFeedbackPresentation::MakeLayout(
            viewport->Pos.x, viewport->Pos.y, viewport->Size.x, viewport->Size.y);
        const char* title = "Copy unavailable";
        const char* detail = "Try an owned item placed on the wall.";
        switch (feedback.status) {
        case FreelancerFeedbackPolicy::Status::CopyRequested:
            title = "Copy requested"; detail = "Original stays on the wall."; break;
        case FreelancerFeedbackPolicy::Status::AlreadyCarried:
            title = "Already carrying this item"; detail = "Freelancer tools can be copied again."; break;
        case FreelancerFeedbackPolicy::Status::Cooldown:
            title = "Please wait"; detail = "Try copying again."; break;
        default: break;
        }
        auto* draw = ImGui::GetForegroundDrawList();
        auto* font = ImGui::GetFont();
        const int alpha = static_cast<int>(255.0f * animation.opacity);
        const ImVec2 position(layout.toastX, layout.toastY + animation.slideOffset);
        const float inset = 14.0f * layout.scale;
        draw->AddLine(position, ImVec2(position.x, position.y + 48.0f * layout.scale),
            IM_COL32(235, 235, 235, alpha), 2.0f * layout.scale);
        const auto text = [&](float offset, float size, const char* value, bool secondary) {
            const ImVec2 at(position.x + inset, position.y + offset * layout.scale);
            draw->AddText(font, size * layout.scale, ImVec2(at.x + 1.0f, at.y + 1.0f),
                IM_COL32(0, 0, 0, alpha), value, nullptr, layout.toastWidth - inset);
            draw->AddText(font, size * layout.scale, at,
                secondary ? IM_COL32(210, 210, 210, alpha) : IM_COL32(255, 255, 255, alpha),
                value, nullptr, layout.toastWidth - inset);
        };
        text(0.0f, 22.0f, title, false);
        text(29.0f, 16.0f, detail, true);
        if (m_lastDrawnSdkGeneration != feedback.generation) {
            m_lastDrawnSdkGeneration = feedback.generation;
            m_diagnostics.Write("sdk-popup-draw-submitted status=" +
                std::to_string(static_cast<unsigned>(feedback.status)));
        }
    }

    ~FreelancerOwnedWallCopy() override {
        UnregisterUpdateCallback();
        if (m_signalHookRegistered && Hooks::SignalOutputPin)
            Hooks::SignalOutputPin->RemoveDetoursWithContext(this);
    }

private:
    static uint64_t NowMs() {
        return static_cast<uint64_t>(GetTickCount64());
    }

    static constexpr uint64_t kSdkFeedbackDurationMs = 2800;
    struct SdkFeedbackSnapshot {
        FreelancerFeedbackPolicy::Status status = FreelancerFeedbackPolicy::Status::Hidden;
        uint64_t changedAtMs = 0;
        uint64_t generation = 0;
        char scene[FreelancerCopyQueue::kSceneCapacity]{};
    };

    SdkFeedbackSnapshot ReadSdkFeedback() const {
        std::lock_guard<std::mutex> lock(m_sdkFeedbackMutex);
        return m_sdkFeedback;
    }

    void ClearSdkFeedback() {
        std::lock_guard<std::mutex> lock(m_sdkFeedbackMutex);
        m_sdkFeedback.status = FreelancerFeedbackPolicy::Status::Hidden;
        m_sdkFeedback.scene[0] = '\0';
    }

    void PublishSdkFeedback(FreelancerFeedbackPolicy::Status status, const std::string& scene) {
        if (scene.empty() || scene.size() >= FreelancerCopyQueue::kSceneCapacity) return;
        {
            std::lock_guard<std::mutex> lock(m_sdkFeedbackMutex);
            m_sdkFeedback.status = status;
            m_sdkFeedback.changedAtMs = NowMs();
            ++m_sdkFeedback.generation;
            std::memcpy(m_sdkFeedback.scene, scene.c_str(), scene.size() + 1);
        }
        m_diagnostics.Write("sdk-popup-queued status=" + std::to_string(static_cast<unsigned>(status)));
    }

    struct QueuedNativeNotice {
        FreelancerFeedbackPolicy::Status status = FreelancerFeedbackPolicy::Status::Hidden;
        uint64_t rootInstanceToken = 0;
        uint64_t queuedAtMs = 0;
        char scene[FreelancerCopyQueue::kSceneCapacity]{};
        bool pending = false;
    };

    struct PromptFeedbackSnapshot {
        uint64_t rootInstanceToken = 0;
        uint64_t originalResourceId = UINT64_MAX;
        uint64_t feedbackResourceId = UINT64_MAX;
        uint64_t expiresAtMs = 0;
        uint64_t lastRestoreAttemptMs = 0;
        uint8_t restoreAttempts = 0;
        char scene[FreelancerCopyQueue::kSceneCapacity]{};
        bool pending = false;
        bool waitForRootSelection = false;
        bool waitForSelectionCycle = false;
    };

    struct QueuedPromptRestore {
        uint64_t rootInstanceToken = 0;
        char scene[FreelancerCopyQueue::kSceneCapacity]{};
        bool pending = false;
    };

    PromptFeedbackSnapshot* FindPromptFeedback(uint64_t rootToken, const std::string& scene) {
        const auto found = std::find_if(m_promptFeedbacks.begin(), m_promptFeedbacks.end(),
            [rootToken, &scene](const PromptFeedbackSnapshot& snapshot) {
                return snapshot.pending && snapshot.rootInstanceToken == rootToken &&
                    std::string_view(snapshot.scene) == scene;
            });
        return found == m_promptFeedbacks.end() ? nullptr : &*found;
    }

    PromptFeedbackSnapshot* FindFreePromptFeedback() {
        const auto found = std::find_if(m_promptFeedbacks.begin(), m_promptFeedbacks.end(),
            [](const PromptFeedbackSnapshot& snapshot) { return !snapshot.pending; });
        return found == m_promptFeedbacks.end() ? nullptr : &*found;
    }

    void ClearAllPromptFeedback() {
        m_promptFeedbacks.clear();
    }

    void QueuePromptRestoreFromHook(const ZEntityRef& root, const std::string& scene) {
        if (!root || scene.empty() || scene.size() >= FreelancerCopyQueue::kSceneCapacity)
            return;
        const uint64_t rootToken = RuntimeInstanceToken(root);
        if (!rootToken)
            return;
        std::lock_guard<std::mutex> lock(m_promptRestoreMutex);
        for (auto& request : m_promptRestoreRequests) {
            if (request.pending && request.rootInstanceToken == rootToken &&
                std::string_view(request.scene) == scene)
                return;
        }
        const auto free = std::find_if(m_promptRestoreRequests.begin(), m_promptRestoreRequests.end(),
            [](const QueuedPromptRestore& request) { return !request.pending; });
        if (free == m_promptRestoreRequests.end()) {
            m_diagnostics.Write("prompt-feedback-restore-queue-full");
            return;
        }
        free->rootInstanceToken = rootToken;
        std::memcpy(free->scene, scene.c_str(), scene.size() + 1);
        free->pending = true;
    }

    void ApplyQueuedPromptRestoreRequests() {
        std::array<QueuedPromptRestore, 8> requests{};
        {
            std::lock_guard<std::mutex> lock(m_promptRestoreMutex);
            requests = m_promptRestoreRequests;
            m_promptRestoreRequests = {};
        }
        for (const auto& request : requests) {
            if (!request.pending)
                continue;
            auto* feedback = FindPromptFeedback(request.rootInstanceToken, request.scene);
            if (!feedback)
                continue;
            feedback->waitForRootSelection = true;
            m_diagnostics.Write("prompt-feedback-restore-queued reason=vanilla-take root-token=" +
                HexId(request.rootInstanceToken));
        }
    }

    void QueueNativeNotification(FreelancerFeedbackPolicy::Status status,
        uint64_t rootInstanceToken, const std::string& scene) {
        if (status == FreelancerFeedbackPolicy::Status::Hidden || !rootInstanceToken ||
            scene.empty() || scene.size() >= FreelancerCopyQueue::kSceneCapacity)
            return;
        std::lock_guard<std::mutex> lock(m_noticeMutex);
        m_nativeNotice = {};
        m_nativeNotice.status = status;
        m_nativeNotice.rootInstanceToken = rootInstanceToken;
        m_nativeNotice.queuedAtMs = NowMs();
        std::memcpy(m_nativeNotice.scene, scene.c_str(), scene.size() + 1);
        m_nativeNotice.pending = true;
    }

    bool HasQueuedNativeNotification() const {
        std::lock_guard<std::mutex> lock(m_noticeMutex);
        return m_nativeNotice.pending;
    }

    bool TakeQueuedNativeNotification(QueuedNativeNotice& notice) {
        std::lock_guard<std::mutex> lock(m_noticeMutex);
        if (!m_nativeNotice.pending)
            return false;
        notice = m_nativeNotice;
        m_nativeNotice = {};
        return NowMs() >= notice.queuedAtMs && NowMs() - notice.queuedAtMs <= 2000;
    }

    void ClearQueuedNativeNotification() {
        std::lock_guard<std::mutex> lock(m_noticeMutex);
        m_nativeNotice = {};
    }

    void SignalNativeNotification(const ZEntityRef& root, uint64_t expectedRootToken,
        const std::string& scene, FreelancerFeedbackPolicy::Status status) {
        if (!IsScenePlaying() || !IsSafehouseScene(scene) || scene.empty() ||
            scene.size() >= FreelancerCopyQueue::kSceneCapacity)
            return;
        const bool sameScene = CurrentSceneKey() == scene;
        if (!sameScene || !root)
            return;
        const uint64_t rootToken = RuntimeInstanceToken(root);
        if (!rootToken || rootToken != expectedRootToken)
            return;
        const uint64_t runtimeRootId = EntityId(root);
        if (!runtimeRootId)
            return;
        const bool patchMarkerPresent = HasTypedPatchPresenceAlias(root);
        if (!patchMarkerPresent)
            return;
        const bool hideNotifications = ReadTypedBoolAlias(root, "WallCopyHideNotifications") == AliasBoolResult::TrueValue;
        const FreelancerFeedbackPolicy::PromptFeedbackRoute route{
            status, true, true, sameScene, true, patchMarkerPresent, hideNotifications,
            runtimeRootId, rootToken, expectedRootToken};
        if (!FreelancerFeedbackPolicy::CanApplyPromptFeedback(route)) {
            m_diagnostics.Write("prompt-feedback-suppressed status=" +
                std::to_string(static_cast<unsigned>(status)) + " runtime-root=" + HexId(runtimeRootId) +
                " root-token=" + HexId(rootToken) + " expected-token=" + HexId(expectedRootToken) +
                " scene-match=" + std::to_string(sameScene) + " hidden=" + std::to_string(hideNotifications));
            return;
        }
        // Original take changes wall eligibility; its configured prompt is left intact.
        if (status == FreelancerFeedbackPolicy::Status::OriginalTakeRequested)
            return;
        PublishSdkFeedback(status, scene);
    }

    void RequestPromptRestoreBeforeVanillaTake(const ZEntityRef& root, const std::string& scene) {
        // This output hook may run outside the update callback. It writes only a
        // bounded token/scene request; prompt ledger access and SetProperty stay on update.
        QueuePromptRestoreFromHook(root, scene);
    }

    // Called only from OnPlayUpdate, immediately before sending the vanilla take input.
    void RestorePromptFeedbackBeforeTake(const ZEntityRef& root, uint64_t expectedRootToken,
        const std::string& scene) {
        if (!root || !IsScenePlaying() || !IsSafehouseScene(scene) ||
            scene.empty() || CurrentSceneKey() != scene)
            return;
        const uint64_t rootToken = RuntimeInstanceToken(root);
        auto* feedback = FindPromptFeedback(rootToken, scene);
        if (!rootToken || rootToken != expectedRootToken || !feedback ||
            !HasTypedPatchPresenceAlias(root))
            return;
        ZRuntimeResourceID currentPrompt;
        if (!ReadTypedResourceAlias(root, "WallCopyPromptText", currentPrompt) ||
            currentPrompt.GetID() != feedback->feedbackResourceId)
            return;
        bool setterAccepted = false;
        const ZRuntimeResourceID original(feedback->originalResourceId);
        const bool setterSucceeded = SetTypedResourceAlias(root, "WallCopyPromptText", original, setterAccepted);
        const bool readbackMatches = setterSucceeded && VerifyTypedResourceAlias(root, "WallCopyPromptText",
            original.GetID(), scene, rootToken);
        m_diagnostics.Write("prompt-feedback-restored-before-take root-token=" + HexId(rootToken) +
            " setter=" + std::to_string(setterAccepted) + " readback=" + std::to_string(readbackMatches));
        if (readbackMatches)
            *feedback = {};
        else {
            feedback->waitForRootSelection = true;
            feedback->waitForSelectionCycle = true;
        }
    }

    void RestorePromptFeedbackIfNeeded(const std::string& scene, bool playing,
        bool focused, bool unpaused, bool selected, uint64_t selectedRootToken) {
        if (m_promptFeedbacks.empty())
            return;
        if (!playing) {
            ClearAllPromptFeedback();
            m_diagnostics.Write("prompt-feedback-cleared reason=nonplaying-stage");
            return;
        }
        if (!IsSafehouseScene(scene)) {
            ClearAllPromptFeedback();
            m_diagnostics.Write("prompt-feedback-cleared reason=scene-changed");
            return;
        }
        const uint64_t now = NowMs();
        for (auto& feedback : m_promptFeedbacks) {
            if (!feedback.pending)
                continue;
            if (std::string_view(feedback.scene) != scene) {
                feedback = {};
                continue;
            }
            const bool sameRootSelected = selected && selectedRootToken == feedback.rootInstanceToken;
            if (!sameRootSelected) {
                // No engine scan while this instance is not selected. Preserve its
                // scalar baseline so it can be restored if the original is returned.
                feedback.waitForRootSelection = true;
                feedback.waitForSelectionCycle = false;
                continue;
            }
            if (!FreelancerFeedbackPolicy::CanScanPromptForRestore(
                    true, true, sameRootSelected, feedback.waitForSelectionCycle)) {
                continue;
            }
            const bool expired = now >= feedback.expiresAtMs;
            const bool contextLost = !focused || !unpaused;
            if (!FreelancerFeedbackPolicy::ShouldAttemptPromptRestore(
                    true, expired, feedback.waitForRootSelection, contextLost))
                continue;
            if (feedback.restoreAttempts != 0 &&
                (now < feedback.lastRestoreAttemptMs || now - feedback.lastRestoreAttemptMs < 500))
                return;
            feedback.lastRestoreAttemptMs = now;
            ++feedback.restoreAttempts;

            if (CurrentSceneKey() != scene) {
                ClearAllPromptFeedback();
                m_diagnostics.Write("prompt-feedback-cleared reason=scene-invalid-before-restore");
                return;
            }
            const auto current = ScanOwnedWallItems(FreelancerTreePolicy::ScanTrigger::CopyClick, m_diagnostics);
            if (CurrentSceneKey() != scene) {
                ClearAllPromptFeedback();
                m_diagnostics.Write("prompt-feedback-cleared reason=scene-changed-during-restore");
                return;
            }
            const auto candidate = std::find_if(current.candidates.begin(), current.candidates.end(),
                [&feedback](const Candidate& value) {
                    return value.rootInstanceToken == feedback.rootInstanceToken && value.rootEntity;
                });
            if (!current.scenePlaying || !current.safehouseScene || candidate == current.candidates.end()) {
                m_diagnostics.Write("prompt-feedback-restore-deferred reason=root-no-longer-eligible root-token=" +
                    HexId(feedback.rootInstanceToken));
                feedback.waitForRootSelection = true;
                feedback.waitForSelectionCycle = true;
                return;
            }
            const auto& root = candidate->rootEntity;
            const uint64_t rootToken = RuntimeInstanceToken(root);
            if (!rootToken || rootToken != feedback.rootInstanceToken) {
                m_diagnostics.Write("prompt-feedback-restore-skipped reason=runtime-root-changed expected-token=" +
                    HexId(feedback.rootInstanceToken));
                feedback = {};
                return;
            }
            const uint64_t runtimeRootId = EntityId(root);
            const bool markerPresent = HasTypedPatchPresenceAlias(root);
            ZRuntimeResourceID currentPrompt;
            const bool promptReadable = markerPresent && ReadTypedResourceAlias(root, "WallCopyPromptText", currentPrompt);
            const bool currentStillFeedback = promptReadable && currentPrompt.GetID() == feedback.feedbackResourceId;
            if (!FreelancerFeedbackPolicy::CanRestorePromptFeedback(
                    current.scenePlaying, current.safehouseScene, true,
                    runtimeRootId != 0, markerPresent, true, currentStillFeedback)) {
                m_diagnostics.Write("prompt-feedback-restore-skipped reason=identity-or-resource-changed root-token=" +
                    HexId(feedback.rootInstanceToken) + " readable=" + std::to_string(promptReadable) +
                    " matches=" + std::to_string(currentStillFeedback));
                feedback = {};
                return;
            }
            bool setterAccepted = false;
            const ZRuntimeResourceID original(feedback.originalResourceId);
            const bool setterSucceeded = SetTypedResourceAlias(root, "WallCopyPromptText", original, setterAccepted);
            const bool readbackMatches = setterSucceeded && VerifyTypedResourceAlias(root, "WallCopyPromptText",
                original.GetID(), scene, rootToken);
            const auto restoredResourceId = readbackMatches ? original.GetID() : currentPrompt.GetID();
            m_diagnostics.Write("prompt-feedback-restored root=" + HexId(runtimeRootId) +
                " root-token=" + HexId(feedback.rootInstanceToken) +
                " resource=" + HexId(restoredResourceId) + " setter=" + std::to_string(setterAccepted) +
                " readback=" + std::to_string(readbackMatches));
            if (readbackMatches)
                feedback = {};
            else {
                feedback.waitForRootSelection = true;
                feedback.waitForSelectionCycle = true;
            }
            return;
        }
    }

    bool ProcessQueuedNativeNotification(const std::string& scene, const ZEntityRef* alreadyResolvedRoot = nullptr,
        uint64_t alreadyResolvedRootToken = 0) {
        QueuedNativeNotice notice;
        if (!TakeQueuedNativeNotification(notice))
            return false;
        if (!IsScenePlaying() || !IsSafehouseScene(scene) || scene != notice.scene) {
            m_diagnostics.Write("native-notification-dropped reason=scene-or-stage");
            return true;
        }
        if (alreadyResolvedRoot && alreadyResolvedRootToken == notice.rootInstanceToken) {
            SignalNativeNotification(*alreadyResolvedRoot, notice.rootInstanceToken, scene, notice.status);
            return true;
        }
        const auto current = ScanOwnedWallItems(FreelancerTreePolicy::ScanTrigger::CopyClick, m_diagnostics);
        const auto candidate = std::find_if(current.candidates.begin(), current.candidates.end(),
            [&notice](const Candidate& value) { return value.rootInstanceToken == notice.rootInstanceToken; });
        if (candidate == current.candidates.end() || !candidate->rootEntity) {
            m_diagnostics.Write("native-notification-dropped reason=root-no-longer-eligible");
            return true;
        }
        SignalNativeNotification(candidate->rootEntity, candidate->rootInstanceToken, scene, notice.status);
        return true;
    }

    void RegisterSignalHook() {
        if (m_signalHookRegistered) return;
        if (!Hooks::SignalOutputPin) {
            m_diagnostics.Write("signal-hook-unavailable fail-closed");
            return;
        }
        Hooks::SignalOutputPin->AddDetour(this, &OnSignalOutputPin);
        m_signalHookRegistered = true;
        m_diagnostics.Write("signal-hook-registered");
    }

    void RegisterUpdateCallback() {
        if (m_updateRegistered) return;
        auto* manager = Globals::GameLoopManager;
        if (!manager || !manager->m_pUpdateEventContainer ||
            !Hooks::ZUpdateEventContainer_AddDelegate || !Hooks::ZUpdateEventContainer_RemoveDelegate) {
            m_diagnostics.Write("play-update-unavailable fail-closed");
            return;
        }
        manager->RegisterFrameUpdate(m_updateDelegate, 0, EUpdateMode::eUpdatePlayMode);
        m_registeredManager = manager;
        m_updateRegistered = true;
        m_diagnostics.Write("play-update-registered mode=play");
    }

    void UnregisterUpdateCallback() {
        if (!m_updateRegistered || !m_registeredManager ||
            !Hooks::ZUpdateEventContainer_RemoveDelegate ||
            Globals::GameLoopManager != m_registeredManager) return;
        m_registeredManager->UnregisterFrameUpdate(m_updateDelegate, 0, EUpdateMode::eUpdatePlayMode);
        m_registeredManager = nullptr;
        m_updateRegistered = false;
    }

    static HookResult<bool> OnSignalOutputPin(void* context, Hook<bool(ZEntityRef, uint32_t, const ZObjectRef&)>*,
        ZEntityRef entity, uint32_t pinId, const ZObjectRef& data) {
        auto* self = static_cast<FreelancerOwnedWallCopy*>(context);
        if (!self || !self->m_updateRegistered || !entity)
            return HookResult<bool>(HookAction::Continue{});
        const auto markerOnValuePin = Hash::Crc32("OnValue");
        const bool isPatchMarkerEvent = EntityId(entity) == FreelancerCopyPolicy::kSelectedLatchEntity &&
            pinId == markerOnValuePin;
        const auto actionTriggeredPin = Hash::Crc32("ActionTriggered");
        if (!isPatchMarkerEvent && (!FreelancerCopyPolicy::PromptPinMatches(pinId, actionTriggeredPin) ||
            !FreelancerCopyPolicy::PromptEntityMatches(EntityId(entity))))
            return HookResult<bool>(HookAction::Continue{});
        if (!IsScenePlaying()) {
            if (isPatchMarkerEvent) self->ClearSelection();
            return HookResult<bool>(HookAction::Continue{});
        }
        const auto scene = CurrentSceneKey();
        if (!IsSafehouseScene(scene)) {
            if (isPatchMarkerEvent) self->ClearSelection();
            return HookResult<bool>(HookAction::Continue{});
        }

        if (isPatchMarkerEvent) {
            const auto sound = entity.GetLogicalParent();
            const auto root = sound ? sound.GetLogicalParent() : ZEntityRef{};
            if (!sound || EntityId(sound) != FreelancerCopyPolicy::kMenuSoundEntity ||
                !root || entity.GetOwningEntity() != root || !EntityId(root)) {
                self->ClearSelection();
                self->m_diagnostics.Write("selection-latch-rejected reason=wrong-root");
                return HookResult<bool>(HookAction::Continue{});
            }
            if (ReadBoolProperty(entity) != BoolReadResult::TrueValue) {
                self->ClearSelectionForRoot(RuntimeInstanceToken(root));
                return HookResult<bool>(HookAction::Continue{});
            }
            ZRepositoryID selectedId;
            size_t selectedMatches = 0;
            const auto selection = self->ResolveClickedOriginalItem(root, selectedId, selectedMatches);
            const bool selectionChanged = selection.result == FreelancerActionMappingPolicy::Result::Unique ?
                self->SetSelection(RuntimeInstanceToken(root), FreelancerCopyPolicy::kMenuEntity, selectedId, scene) :
                self->ClearSelectionForRoot(RuntimeInstanceToken(root));
            if (selectionChanged) {
                self->m_diagnostics.Write(std::string("selection-latch state=") +
                    (selection.result == FreelancerActionMappingPolicy::Result::Unique ? "selected" : "cleared") +
                    " map=" + std::to_string(static_cast<int>(selection.result)) +
                    " candidates=" + std::to_string(selectedMatches));
            }
            return HookResult<bool>(HookAction::Continue{});
        }

        if (!FreelancerCopyPolicy::PromptEntityMatches(EntityId(entity)))
            return HookResult<bool>(HookAction::Continue{});

        ZEntityRef root;
        if (!self->ResolvePromptMenu(entity, pinId, root))
            return HookResult<bool>(HookAction::Continue{});
        const bool defaultKeybinds = ReadTypedBoolAlias(root, "WallCopyDefaultKeybinds") == AliasBoolResult::TrueValue;
        if (FreelancerCopyPolicy::ResolveConfiguredAccept(true, true, defaultKeybinds, true) ==
            FreelancerCopyPolicy::AcceptDisposition::Vanilla) {
            self->m_diagnostics.Write("prompt-event mode=default-keybinds vanilla-take");
            return HookResult<bool>(HookAction::Continue{});
        }
        ZRepositoryID repositoryId;
        size_t matchingItems = 0;
        const auto mapping = self->ResolveClickedOriginalItem(root, repositoryId, matchingItems);
        const auto acceptDisposition = FreelancerCopyPolicy::ResolveConfiguredAccept(
            true, true, false, mapping.result == FreelancerActionMappingPolicy::Result::Unique);
        if (acceptDisposition == FreelancerCopyPolicy::AcceptDisposition::Blocked) {
            const char* reason = mapping.result == FreelancerActionMappingPolicy::Result::Ambiguous ? "ambiguous" :
                mapping.result == FreelancerActionMappingPolicy::Result::EmptyId ? "empty-id" :
                mapping.result == FreelancerActionMappingPolicy::Result::TooManyActions ? "action-limit" : "no-match";
            if (mapping.result == FreelancerActionMappingPolicy::Result::TypeRegistryUnavailable)
                reason = "type-registry-unavailable";
            self->m_diagnostics.Write(std::string("prompt-source-rejected reason=") + reason +
                " candidates=" + std::to_string(matchingItems));
            self->QueueNativeNotification(FreelancerFeedbackPolicy::Status::Blocked,
                RuntimeInstanceToken(root), scene);
            // The exact patched prompt says copy. Keep the original on the wall if
            // its stable item mapping cannot be established; never fall through to PickUp.
            return HookResult<bool>(HookAction::Return{}, true);
        }
        repositoryId.m_nHigh = mapping.repositoryHigh;
        repositoryId.m_nLow = mapping.repositoryLow;
        self->m_diagnostics.Write("prompt-source-matched candidates=" + std::to_string(matchingItems));

        const auto result = [&]() {
            std::lock_guard<std::mutex> lock(self->m_queueMutex);
            return self->m_requests.Enqueue(RuntimeInstanceToken(root), repositoryId.m_nHigh, repositoryId.m_nLow, scene.c_str(), NowMs());
        }();
        if (result == FreelancerCopyQueue::EnqueueResult::Cooldown)
            self->QueueNativeNotification(FreelancerFeedbackPolicy::Status::Cooldown,
                RuntimeInstanceToken(root), scene);
        else if (result == FreelancerCopyQueue::EnqueueResult::Full ||
                 result == FreelancerCopyQueue::EnqueueResult::Invalid)
            self->QueueNativeNotification(FreelancerFeedbackPolicy::Status::Blocked,
                RuntimeInstanceToken(root), scene);
        self->m_diagnostics.Write("prompt-event repo=" +
            std::string(repositoryId.ToString(ZGuid::GuidFormat::NoDashes).c_str()) +
            " queued=" + std::to_string(static_cast<int>(result)));
        // Consume only this exact validated prompt; the marker edge is inert if the hook is absent.
        return HookResult<bool>(HookAction::Return{}, true);
    }

    static bool IsScenePlaying() {
        auto* module = Globals::Hitman5Module;
        return module && module->IsEngineInitialized() && module->m_pEntitySceneContext &&
            module->m_pEntitySceneContext->m_LoadingStage == ESceneLoadingStage::eLoading_ScenePlaying;
    }

    static bool IsSafehouseScene(const std::string& scene) {
        return scene.starts_with("assembly:/_pro/scenes/missions/snug/");
    }

    static std::string CurrentSceneKey() {
        auto* module = Globals::Hitman5Module;
        if (!module || !module->IsEngineInitialized() || !module->m_pEntitySceneContext) return {};
        const auto& scene = module->m_pEntitySceneContext->m_SceneInitParameters.m_SceneResource;
        return scene.size() ? std::string(scene.c_str(), scene.size()) : std::string{};
    }

    enum class AliasBoolResult { Missing, WrongType, Unreadable, FalseValue, TrueValue };

    static AliasBoolResult ReadTypedBoolAlias(const ZEntityRef& entity, const char* alias) {
        if (!entity || !entity->GetType() || !alias || !Globals::MemoryManager || !*Globals::MemoryManager)
            return AliasBoolResult::Unreadable;
        const uint32_t propertyId = Hash::Crc32(alias);
        const auto* property = entity->GetType()->FindProperty(propertyId);
        if (!property || !property->m_pPropertyInfo) return AliasBoolResult::Missing;
        const auto* named = property->GetPropertyInfo();
        const auto* type = named && named->m_propertyInfo.m_Type ? named->m_propertyInfo.m_Type->GetTypeInfo() : nullptr;
        if (!type || !type->pszTypeName || Hash::Crc32(type->pszTypeName) != ZHMTypeId<bool> ||
            type->m_nTypeSize != sizeof(bool)) return AliasBoolResult::WrongType;
        const auto value = entity.GetProperty<bool>(propertyId);
        const auto* boolean = value.As<bool>();
        if (!boolean) return AliasBoolResult::Unreadable;
        return *boolean ? AliasBoolResult::TrueValue : AliasBoolResult::FalseValue;
    }

    static uint64_t EntityId(const ZEntityRef& entity) {
        const auto* type = entity ? entity->GetType() : nullptr;
        return type ? type->m_nEntityID : 0;
    }

    struct SelectedWallSnapshot {
        uint64_t rootInstanceToken = 0;
        uint64_t menuId = 0;
        uint64_t repositoryHigh = 0;
        uint64_t repositoryLow = 0;
        char scene[FreelancerCopyQueue::kSceneCapacity]{};
        bool valid = false;
    };

    SelectedWallSnapshot GetSelection() const {
        std::lock_guard<std::mutex> lock(m_selectionMutex);
        SelectedWallSnapshot snapshot;
        snapshot.rootInstanceToken = m_selectionRootToken;
        snapshot.menuId = m_selectionMenuId;
        snapshot.repositoryHigh = m_selectionHigh;
        snapshot.repositoryLow = m_selectionLow;
        std::memcpy(snapshot.scene, m_selectionScene, sizeof(snapshot.scene));
        snapshot.valid = m_hasSelection;
        return snapshot;
    }

    void ClearSelection() {
        std::lock_guard<std::mutex> lock(m_selectionMutex);
        m_hasSelection = false;
        m_selectionRootToken = m_selectionMenuId = m_selectionHigh = m_selectionLow = 0;
        m_selectionScene[0] = '\0';
    }

    bool ResolvePromptMenu(const ZEntityRef& entity, uint32_t pinId, ZEntityRef& root) const {
        constexpr uint32_t triggeredPin = Hash::Crc32("ActionTriggered");
        if (!entity || !FreelancerCopyPolicy::PromptEntityMatches(EntityId(entity)) ||
            !FreelancerCopyPolicy::PromptPinMatches(pinId, triggeredPin)) return false;
        const auto wall = entity.GetLogicalParent();
        if (!wall) return false;
        const auto menu = wall.GetLogicalParent();
        if (!menu) return false;
        root = menu.GetLogicalParent();
        if (!root) return false;
        const bool patchMarkerPresent = HasTypedPatchPresenceAlias(root);
        return FreelancerCopyPolicy::ExactPromptRoute(EntityId(entity), pinId,
            EntityId(wall), EntityId(menu), RuntimeInstanceToken(root),
            wall.GetOwningEntity() == root, menu.GetOwningEntity() == root,
            patchMarkerPresent, triggeredPin);
    }

    bool HasTypedResourceAlias(const ZEntityRef& entity, const char* alias) const {
        if (!entity || !entity->GetType() || !alias)
            return false;
        const auto* property = entity->GetType()->FindProperty(Hash::Crc32(alias));
        const auto* named = property && property->m_pPropertyInfo ? property->GetPropertyInfo() : nullptr;
        const auto* type = named && named->m_propertyInfo.m_Type ? named->m_propertyInfo.m_Type->GetTypeInfo() : nullptr;
        return type && type->pszTypeName && std::strcmp(type->pszTypeName, "ZRuntimeResourceID") == 0 &&
            type->m_nTypeSize == sizeof(ZRuntimeResourceID) && type->m_nTypeAlignment == alignof(ZRuntimeResourceID);
    }

    bool ReadTypedResourceAlias(const ZEntityRef& entity, const char* alias, ZRuntimeResourceID& value) const {
        if (!Globals::TypeRegistry || !*Globals::TypeRegistry || !Globals::MemoryManager || !*Globals::MemoryManager ||
            !HasTypedResourceAlias(entity, alias))
            return false;
        const auto property = entity.GetProperty<ZRuntimeResourceID>(Hash::Crc32(alias));
        const auto* resource = property.As<ZRuntimeResourceID>();
        if (!resource || resource->GetID() == UINT64_MAX)
            return false;
        value = *resource;
        return true;
    }

    bool SetTypedResourceAlias(ZEntityRef entity, const char* alias,
        const ZRuntimeResourceID& value, bool& setterAccepted) const {
        setterAccepted = false;
        if (!Hooks::SetPropertyValue || !Globals::TypeRegistry || !*Globals::TypeRegistry ||
            !Globals::MemoryManager || !*Globals::MemoryManager || !HasTypedResourceAlias(entity, alias))
            return false;
        const uint32_t propertyId = Hash::Crc32(alias);
        setterAccepted = entity.SetProperty<ZRuntimeResourceID>(propertyId, value, true);
        return setterAccepted;
    }

    bool VerifyTypedResourceAlias(const ZEntityRef& entity, const char* alias,
        uint64_t expectedResourceId, const std::string& scene, uint64_t expectedRootToken) const {
        if (!IsScenePlaying() || scene.empty() || CurrentSceneKey() != scene)
            return false;
        const uint64_t rootToken = RuntimeInstanceToken(entity);
        if (!rootToken || rootToken != expectedRootToken)
            return false;
        ZRuntimeResourceID readback;
        return ReadTypedResourceAlias(entity, alias, readback) && readback.GetID() == expectedResourceId;
    }

    bool HasTypedPatchPresenceAlias(const ZEntityRef& root) const {
        if (!root || !root->GetType())
            return false;
        const auto* property = root->GetType()->FindProperty(Hash::Crc32("WallCopyPatchInstalled"));
        const auto* info = property && property->m_pPropertyInfo ? property->GetPropertyInfo() : nullptr;
        const auto* reflectedType = info && info->m_propertyInfo.m_Type ? info->m_propertyInfo.m_Type->GetTypeInfo() : nullptr;
        return reflectedType && reflectedType->pszTypeName &&
            Hash::Crc32(reflectedType->pszTypeName) == ZHMTypeId<bool> &&
            reflectedType->m_nTypeSize == sizeof(bool);
    }

    bool HasSelectedPatchedRoot(uint64_t rootInstanceToken) const {
        std::lock_guard<std::mutex> lock(m_selectionMutex);
        return m_hasSelection && m_selectionRootToken == rootInstanceToken;
    }

    static bool IsGameForeground() {
        const HWND foreground = GetForegroundWindow();
        if (!foreground)
            return false;
        DWORD processId = 0;
        GetWindowThreadProcessId(foreground, &processId);
        return processId == GetCurrentProcessId();
    }

    void TryTakeOriginal(const std::string& scene) {
        const auto selection = GetSelection();
        if (!selection.valid || selection.scene[0] == '\0' || scene != selection.scene)
            return;
        ZRepositoryID repositoryId;
        repositoryId.m_nHigh = selection.repositoryHigh;
        repositoryId.m_nLow = selection.repositoryLow;
        const auto current = ScanOwnedWallItems(FreelancerTreePolicy::ScanTrigger::CopyClick, m_diagnostics);
        const auto eligible = std::find_if(current.candidates.begin(), current.candidates.end(),
            [&selection, &repositoryId](const Candidate& candidate) {
                return candidate.rootInstanceToken == selection.rootInstanceToken && candidate.repositoryId == repositoryId && candidate.rootEntity;
            });
        if (!current.scenePlaying || !current.safehouseScene || eligible == current.candidates.end()) {
            m_diagnostics.Write("original-take-blocked reason=wall-item-not-currently-eligible");
            return;
        }
        const bool patchPresent = HasTypedPatchPresenceAlias(eligible->rootEntity);
        const bool defaultKeybinds = ReadTypedBoolAlias(eligible->rootEntity, "WallCopyDefaultKeybinds") ==
            AliasBoolResult::TrueValue;
        const bool canPressP = FreelancerCopyPolicy::CanTriggerPAction(
            current.scenePlaying, current.safehouseScene, IsGameForeground(),
            Globals::InputActionManager && Globals::InputActionManager->m_bEnabled,
            Globals::GameTimeManager && !Globals::GameTimeManager->m_bPaused,
            selection.valid, true, patchPresent, true);
        if (!canPressP) {
            if (patchPresent)
                SignalNativeNotification(eligible->rootEntity, eligible->rootInstanceToken, scene,
                    FreelancerFeedbackPolicy::Status::Blocked);
            m_diagnostics.Write("original-take-blocked reason=policy patch=" + std::to_string(patchPresent));
            return;
        }
        if (FreelancerCopyPolicy::ResolvePAction(defaultKeybinds) == FreelancerCopyPolicy::PAction::Copy) {
            FreelancerCopyQueue::EnqueueResult queued;
            {
                std::lock_guard<std::mutex> lock(m_queueMutex);
                queued = m_requests.Enqueue(selection.rootInstanceToken, repositoryId.m_nHigh, repositoryId.m_nLow,
                    scene.c_str(), NowMs());
            }
            if (queued == FreelancerCopyQueue::EnqueueResult::Cooldown)
                SignalNativeNotification(eligible->rootEntity, eligible->rootInstanceToken, scene,
                    FreelancerFeedbackPolicy::Status::Cooldown);
            else if (queued == FreelancerCopyQueue::EnqueueResult::Full ||
                     queued == FreelancerCopyQueue::EnqueueResult::Invalid)
                SignalNativeNotification(eligible->rootEntity, eligible->rootInstanceToken, scene,
                    FreelancerFeedbackPolicy::Status::Blocked);
            m_diagnostics.Write("original-key-copy-queued result=" + std::to_string(static_cast<int>(queued)) +
                " repo=" + std::string(repositoryId.ToString(ZGuid::GuidFormat::NoDashes).c_str()));
            return;
        }
        // The root input forwarding mirrors the stock menu's exact PickUp and PickedUp paths.
        eligible->rootEntity.SignalInputPin(Hash::Crc32("WallCopyTakeOriginal"));
        m_diagnostics.Write("original-take-sent repo=" +
            std::string(repositoryId.ToString(ZGuid::GuidFormat::NoDashes).c_str()));
    }

    bool SetSelection(uint64_t rootInstanceToken, uint64_t menuId, const ZRepositoryID& itemId, const std::string& scene) {
        if (!rootInstanceToken || !menuId || scene.size() >= FreelancerCopyQueue::kSceneCapacity)
            return false;
        std::lock_guard<std::mutex> lock(m_selectionMutex);
        const bool changed = !m_hasSelection || m_selectionRootToken != rootInstanceToken ||
            m_selectionMenuId != menuId || m_selectionHigh != itemId.m_nHigh ||
            m_selectionLow != itemId.m_nLow || std::string_view(m_selectionScene) != scene;
        m_selectionRootToken = rootInstanceToken;
        m_selectionMenuId = menuId;
        m_selectionHigh = itemId.m_nHigh;
        m_selectionLow = itemId.m_nLow;
        std::memcpy(m_selectionScene, scene.c_str(), scene.size() + 1);
        m_hasSelection = true;
        return changed;
    }

    bool ClearSelectionForRoot(uint64_t rootInstanceToken) {
        std::lock_guard<std::mutex> lock(m_selectionMutex);
        if (m_hasSelection && m_selectionRootToken == rootInstanceToken) {
            m_hasSelection = false;
            m_selectionRootToken = m_selectionMenuId = m_selectionHigh = m_selectionLow = 0;
            m_selectionScene[0] = '\0';
            return true;
        }
        return false;
    }

    FreelancerActionMappingPolicy::Resolution ResolveClickedOriginalItem(
        const ZEntityRef& clickedRoot, ZRepositoryID& repositoryId, size_t& matchingItems) const {
        matchingItems = 0;
        auto* manager = Globals::HM5ActionManager;
        if (!manager || !clickedRoot)
            return {FreelancerActionMappingPolicy::Result::NoMatch, 0, 0};
        if (!FreelancerActionMappingPolicy::CanQueryItemInterface(
                Globals::TypeRegistry && *Globals::TypeRegistry))
            return {FreelancerActionMappingPolicy::Result::TypeRegistryUnavailable, 0, 0};
        const size_t actionCount = manager->m_Actions.size();
        if (actionCount > FreelancerActionMappingPolicy::kMaximumActions)
            return {FreelancerActionMappingPolicy::Result::TooManyActions, 0, 0};

        std::vector<FreelancerActionMappingPolicy::Match> matches;
        matches.reserve(4);
        for (size_t i = 0; i < actionCount; ++i) {
            auto* action = manager->m_Actions[i];
            if (!action || action->m_eActionType != EActionType::AT_PICKUP || !action->m_Object)
                continue;
            const auto actionObject = action->m_Object;
            const auto* actionType = actionObject->GetType();
            if (!actionType || !actionType->m_pInterfaceData)
                continue;
            auto* item = actionObject.QueryInterface<ZHM5Item>();
            if (!item || !item->m_rSpawner)
                continue;

            const auto spawner = item->m_rSpawner;
            const auto itemParent = spawner.GetLogicalParent();
            if (!FreelancerActionMappingPolicy::ExactActionRoute(
                    action->m_eActionType == EActionType::AT_PICKUP,
                    item != nullptr,
                    EntityId(spawner) == kWallSpawnerEntityId,
                    itemParent && EntityId(itemParent) == kItemParentEntityId,
                    itemParent && itemParent.GetLogicalParent() == clickedRoot,
                    spawner.GetOwningEntity() == clickedRoot))
                continue;

            ++matchingItems;
            const auto* descriptor = item->m_pItemConfigDescriptor;
            uint64_t repositoryHigh = 0;
            uint64_t repositoryLow = 0;
            if (descriptor) {
                repositoryHigh = descriptor->m_ItemID.m_nHigh;
                repositoryLow = descriptor->m_ItemID.m_nLow;
            }
            matches.push_back({reinterpret_cast<uintptr_t>(actionObject.GetEntity()),
                repositoryHigh, repositoryLow, descriptor != nullptr});
        }
        const auto resolution = FreelancerActionMappingPolicy::Resolve(matches.data(), matches.size(), actionCount);
        if (resolution.result == FreelancerActionMappingPolicy::Result::Unique) {
            repositoryId.m_nHigh = resolution.repositoryHigh;
            repositoryId.m_nLow = resolution.repositoryLow;
        }
        return resolution;
    }

    void OnPlayUpdate(const SGameUpdateEvent&) {
        if (!m_updateRegistered || m_processing.test_and_set()) return;
        struct ClearFlag { std::atomic_flag& flag; ~ClearFlag() { flag.clear(); } } clear{m_processing};
        const bool pDown = (GetAsyncKeyState('P') & 0x8000) != 0;
        const bool pPressed = pDown && !m_pWasDown;
        m_pWasDown = pDown;
        const bool playing = IsScenePlaying();
        const bool focused = IsGameForeground();
        const bool inputEnabled = Globals::InputActionManager && Globals::InputActionManager->m_bEnabled;
        const bool unpaused = Globals::GameTimeManager && !Globals::GameTimeManager->m_bPaused;
        const auto selection = GetSelection();
        const auto feedback = ReadSdkFeedback();
        if (feedback.status != FreelancerFeedbackPolicy::Status::Hidden) {
            const auto now = NowMs();
            if (!playing || !focused || !unpaused || now < feedback.changedAtMs ||
                now - feedback.changedAtMs >= kSdkFeedbackDurationMs ||
                CurrentSceneKey() != feedback.scene)
                ClearSdkFeedback();
        }
        if (pPressed) {
            const char* gate = !playing ? "not-playing" : !focused ? "not-foreground" :
                !inputEnabled ? "sdk-input-disabled" : !unpaused ? "paused" :
                !selection.valid ? "no-current-wall-selection" : nullptr;
            if (gate) {
                m_diagnostics.Write(std::string("original-take-key-blocked reason=") + gate);
            } else {
                const auto sceneForTake = CurrentSceneKey();
                if (!IsSafehouseScene(sceneForTake)) {
                    m_diagnostics.Write("original-take-key-blocked reason=not-safehouse");
                } else {
                    TryTakeOriginal(sceneForTake);
                }
            }
        }
        if (!playing || !focused || !unpaused) {
            if (!playing || !focused || !unpaused) ClearSelection();
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_requests.Size() != 0) {
                m_requests.Clear();
                m_diagnostics.Write("queue-cleared state-transition");
            }
            ClearQueuedNativeNotification();
            if (!playing) return;
        }
        bool hasRequests = false;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            hasRequests = m_requests.Size() != 0;
            if (!playing) {
                m_requests.Clear();
                m_diagnostics.Write("queue-cleared nonplaying-stage");
                return;
            }
        }
        if (!hasRequests && !HasQueuedNativeNotification()) return;
        const auto scene = CurrentSceneKey();
        if (!IsSafehouseScene(scene)) {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            if (m_requests.Size() != 0) {
                m_requests.Clear();
                m_diagnostics.Write("queue-cleared nonsafehouse-scene");
            }
            ClearQueuedNativeNotification();
            return;
        }
        FreelancerCopyQueue::Request request;
        bool hasRequest = false;
        {
            std::lock_guard<std::mutex> lock(m_queueMutex);
            hasRequest = m_requests.Size() != 0 && m_requests.Pop(NowMs(), request);
        }
        ZEntityRef copiedRoot;
        uint64_t copiedRootToken = 0;
        if (hasRequest) {
            if (scene != request.sceneKey) {
                m_diagnostics.Write("request-dropped scene-mismatch");
            } else {
                ZRepositoryID repositoryId;
                repositoryId.m_nHigh = request.repositoryHigh;
                repositoryId.m_nLow = request.repositoryLow;
                m_diagnostics.Write("prompt-update repo=" +
                    std::string(repositoryId.ToString(ZGuid::GuidFormat::NoDashes).c_str()));
                std::string result;
                FreelancerCopyPolicy::CarryDisposition carryDisposition = FreelancerCopyPolicy::CarryDisposition::Unknown;
                const bool copyRequested = CopyCurrentEligibleItem(repositoryId, request.rootInstanceToken,
                    copiedRoot, result, carryDisposition, m_diagnostics);
                const auto status = FreelancerFeedbackPolicy::ResolveCopyResult(copyRequested,
                    carryDisposition == FreelancerCopyPolicy::CarryDisposition::BlockedAlreadyCarried);
                copiedRootToken = copiedRoot ? RuntimeInstanceToken(copiedRoot) : 0;
                SignalNativeNotification(copiedRoot, request.rootInstanceToken, scene, status);
            }
        }
        ProcessQueuedNativeNotification(scene, copiedRoot ? &copiedRoot : nullptr, copiedRootToken);
    }

    ZMemberDelegate<FreelancerOwnedWallCopy, void(const SGameUpdateEvent&)> m_updateDelegate{
        this, &FreelancerOwnedWallCopy::OnPlayUpdate};
    ZGameLoopManager* m_registeredManager = nullptr;
    std::mutex m_queueMutex;
    FreelancerCopyQueue::Queue m_requests;
    FreelancerDiagnostics::BoundedDiagnosticLog m_diagnostics;
    std::atomic_flag m_processing = ATOMIC_FLAG_INIT;
    mutable std::mutex m_sdkFeedbackMutex;
    SdkFeedbackSnapshot m_sdkFeedback;
    bool m_sdkRenderCallbackLogged = false; // Render-thread-owned diagnostics only.
    uint64_t m_lastDrawnSdkGeneration = 0; // Render-thread-owned diagnostics only.
    mutable std::mutex m_noticeMutex;
    QueuedNativeNotice m_nativeNotice;
    std::mutex m_promptRestoreMutex;
    std::array<QueuedPromptRestore, 8> m_promptRestoreRequests{};
    static constexpr size_t kMaxPromptFeedbacks = 8;
    std::vector<PromptFeedbackSnapshot> m_promptFeedbacks;
    mutable std::mutex m_selectionMutex;
    uint64_t m_selectionRootToken = 0;
    uint64_t m_selectionMenuId = 0;
    uint64_t m_selectionHigh = 0;
    uint64_t m_selectionLow = 0;
    char m_selectionScene[FreelancerCopyQueue::kSceneCapacity]{};
    bool m_hasSelection = false;
    bool m_pWasDown = false;
    bool m_signalHookRegistered = false;
    bool m_updateRegistered = false;
};
}

DEFINE_ZHM_PLUGIN(FreelancerOwnedWallCopy)



