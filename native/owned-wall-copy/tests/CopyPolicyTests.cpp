#include "../src/CopyPolicy.h"
#include "../src/ActionMappingPolicy.h"
#include "../src/CopyRequestQueue.h"
#include "../src/TraversalPolicy.h"
#include "../src/FeedbackPolicy.h"

#include <cstdio>
#include <string>
#include <utility>

namespace {
int checks = 0;
int failures = 0;
void Check(const char* name, bool value) {
    ++checks;
    if (!value) { ++failures; std::fprintf(stderr, "FAIL: %s\n", name); }
}
}

int main() {
    using namespace FreelancerCopyPolicy;
    constexpr uint32_t actionTriggered = 0x12345678;
    Check("ActionTriggered scalar pin fast gate accepts exact pin", PromptPinMatches(actionTriggered,actionTriggered));
    Check("other output pin scalar fast gate rejects", !PromptPinMatches(9,actionTriggered));
    Check("stock gear-wall Accept prompt scalar gate accepts", PromptEntityMatches(0x82D0B27F8A85D759ULL));
    Check("legacy Action X copy prompt is not routed", !PromptEntityMatches(0xAE106D7E11ED863EULL));
    Check("other entity scalar gate rejects", !PromptEntityMatches(0x1111ULL));
    Check("patched stock ACCEPT route with arbitrary runtime root accepted", ExactPromptRoute(0x82D0B27F8A85D759ULL,actionTriggered,0x76BC7DEBE63916B6ULL,0xDD218B2743991CA8ULL,0x5A52B69910ABCDEFULL,true,true,true,actionTriggered));
    Check("unpatched helper marker cannot activate copy", !ExactPromptRoute(0x82D0B27F8A85D759ULL,actionTriggered,0x76BC7DEBE63916B6ULL,0xDD218B2743991CA8ULL,0x5A52B69910ABCDEFULL,true,true,false,actionTriggered));
    Check("other prompt entity rejected", !ExactPromptRoute(1,actionTriggered,0x76BC7DEBE63916B6ULL,0xDD218B2743991CA8ULL,0x5A52B69910ABCDEFULL,true,true,true,actionTriggered));
    Check("other output pin rejected", !ExactPromptRoute(0x82D0B27F8A85D759ULL,2,0x76BC7DEBE63916B6ULL,0xDD218B2743991CA8ULL,0x5A52B69910ABCDEFULL,true,true,true,actionTriggered));
    Check("wrong wall parent rejected", !ExactPromptRoute(0x82D0B27F8A85D759ULL,actionTriggered,3,0xDD218B2743991CA8ULL,0x5A52B69910ABCDEFULL,true,true,true,actionTriggered));
    Check("empty runtime root rejected", !ExactPromptRoute(0x82D0B27F8A85D759ULL,actionTriggered,0x76BC7DEBE63916B6ULL,0xDD218B2743991CA8ULL,0,false,false,true,actionTriggered));
    Check("mismatched wall owner rejected", !ExactPromptRoute(0x82D0B27F8A85D759ULL,actionTriggered,0x76BC7DEBE63916B6ULL,0xDD218B2743991CA8ULL,0x5A52B69910ABCDEFULL,false,true,true,actionTriggered));
    Check("mismatched menu owner rejected", !ExactPromptRoute(0x82D0B27F8A85D759ULL,actionTriggered,0x76BC7DEBE63916B6ULL,0xDD218B2743991CA8ULL,0x5A52B69910ABCDEFULL,true,false,true,actionTriggered));
    Check("patched Accept stays intercepted after transient selection state clears", ResolveAccept(true,true,true) == AcceptDisposition::Copy);
    Check("recognized patched Accept preserves original when item mapping fails", ResolveAccept(true,true,false) == AcceptDisposition::Blocked);
    Check("patch-presence alias absent restores vanilla Accept", ResolveAccept(false,true,true) == AcceptDisposition::Vanilla);
    Check("wrong prompt route remains vanilla", ResolveAccept(true,false,true) == AcceptDisposition::Vanilla);
    Check("Default Keybinds ON preserves the vanilla take even when copy mapping is absent",
        ResolveConfiguredAccept(true,true,true,false) == AcceptDisposition::Vanilla);
    Check("Default Keybinds ON bypasses copy mapping and duplicate guards",
        ResolveConfiguredAccept(true,true,true,true) == AcceptDisposition::Vanilla);
    Check("Default Keybinds OFF copies only a mapped exact prompt",
        ResolveConfiguredAccept(true,true,false,true) == AcceptDisposition::Copy);
    Check("Default Keybinds OFF blocks a recognised copy prompt with failed mapping",
        ResolveConfiguredAccept(true,true,false,false) == AcceptDisposition::Blocked);
    Check("Default Keybinds cannot activate without the base wall-copy patch",
        ResolveConfiguredAccept(false,true,true,true) == AcceptDisposition::Vanilla);
    Check("missing query-ready alias cannot authorize a non-tool copy", ResolveCarry(false,true,true,false,false,false) == CarryDisposition::Unknown);
    Check("fresh false ready latch cannot authorize a non-tool copy", ResolveCarry(true,false,true,false,false,false) == CarryDisposition::Unknown);
    Check("unreadable carried alias cannot masquerade as not carried", ResolveCarry(true,true,false,false,false,false) == CarryDisposition::Unknown);
    Check("confirmed not-carried item can be copied without a toolbox flag", ResolveCarry(true,true,true,false,false,false) == CarryDisposition::Copy);
    Check("carried item with unknown toolbox flag is not guessed", ResolveCarry(true,true,true,true,false,false) == CarryDisposition::Unknown);
    Check("typed true toolbox flag exempts tool without inventory query", ResolveCarry(false,false,false,false,true,true) == CarryDisposition::Copy);
    Check("typed false toolbox flag does not exempt a carried item", ResolveCarry(true,true,true,true,true,false) == CarryDisposition::BlockedAlreadyCarried);
    Check("exact wall instance accepted", WallInstanceMatches(true,true,true,true,true,true,true,true));
    Check("wrong spawner rejected", !WallInstanceMatches(false,true,true,true,true,true,true,true));
    Check("wrong parent rejected", !WallInstanceMatches(true,false,true,true,true,true,true,true));
    Check("missing root rejected", !WallInstanceMatches(true,true,false,true,true,true,true,true));
    Check("spawner with another owner rejected", !WallInstanceMatches(true,true,true,false,true,true,true,true));
    Check("wrong typed alias target rejected", !WallInstanceMatches(true,true,true,true,false,true,true,true));
    Check("aliases on another root rejected", !WallInstanceMatches(true,true,true,true,true,true,true,false));

    using FreelancerActionMappingPolicy::Match;
    using FreelancerActionMappingPolicy::Result;
    Check("missing type registry blocks QueryInterface path",
        !FreelancerActionMappingPolicy::CanQueryItemInterface(false));
    Check("available type registry permits QueryInterface path",
        FreelancerActionMappingPolicy::CanQueryItemInterface(true));
    Check("exact pickup item spawner parent and clicked-root route accepted",
        FreelancerActionMappingPolicy::ExactActionRoute(true, true, true, true, true, true));
    Check("wrong action type rejected from mapping scan",
        !FreelancerActionMappingPolicy::ExactActionRoute(false, true, true, true, true, true));
    Check("wrong item type rejected from mapping scan",
        !FreelancerActionMappingPolicy::ExactActionRoute(true, false, true, true, true, true));
    Check("different clicked root rejected from mapping scan",
        !FreelancerActionMappingPolicy::ExactActionRoute(true, true, true, true, false, true));
    Check("spawner with different owner rejected from mapping scan",
        !FreelancerActionMappingPolicy::ExactActionRoute(true, true, true, true, true, false));
    const Match exactItem[]{{0x1000, 0x11, 0x22, true}};
    const auto exactMapping = FreelancerActionMappingPolicy::Resolve(exactItem, 1, 8);
    Check("one exact wall item instance maps its descriptor ID",
        exactMapping.result == Result::Unique && exactMapping.repositoryHigh == 0x11 && exactMapping.repositoryLow == 0x22);
    const Match duplicateEntry[]{{0x1000, 0x11, 0x22, true}, {0x1000, 0x11, 0x22, true}};
    Check("duplicate action entries for the same item collapse safely",
        FreelancerActionMappingPolicy::Resolve(duplicateEntry, 2, 8).result == Result::Unique);
    const Match returnedItemInstances[]{{0x1000, 0x11, 0x22, true}, {0x2000, 0x11, 0x22, true}};
    Check("returned instances with one exact repository ID collapse safely",
        FreelancerActionMappingPolicy::Resolve(returnedItemInstances, 2, 8).result == Result::Unique);
    const Match separateInstances[]{{0x1000, 0x11, 0x22, true}, {0x2000, 0x33, 0x44, true}};
    Check("two distinct exact-root item instances are ambiguous",
        FreelancerActionMappingPolicy::Resolve(separateInstances, 2, 8).result == Result::Ambiguous);
    const Match sameInstanceDifferentId[]{{0x1000, 0x11, 0x22, true}, {0x1000, 0x33, 0x44, true}};
    Check("duplicate instance with conflicting descriptor IDs is ambiguous",
        FreelancerActionMappingPolicy::Resolve(sameInstanceDifferentId, 2, 8).result == Result::Ambiguous);
    const Match emptyDescriptor[]{{0x1000, 0, 0, true}};
    Check("empty descriptor repository ID is rejected",
        FreelancerActionMappingPolicy::Resolve(emptyDescriptor, 1, 8).result == Result::EmptyId);
    const Match missingDescriptor[]{{0x1000, 0, 0, false}};
    Check("missing descriptor is rejected",
        FreelancerActionMappingPolicy::Resolve(missingDescriptor, 1, 8).result == Result::EmptyId);
    Check("action list above bound is rejected before mapping",
        FreelancerActionMappingPolicy::Resolve(exactItem, 1, 4097).result == Result::TooManyActions);
    Check("no exact-root item match is rejected",
        FreelancerActionMappingPolicy::Resolve(nullptr, 0, 8).result == Result::NoMatch);
    Check("eligible owned placed wall accepted", Eligible(true,true,true,true,true,true,true,true,true,true));
    Check("unknown wall flag rejected", !Eligible(true,true,true,true,false,false,true,true,true,true));
    Check("false owned flag rejected", !Eligible(true,true,true,true,true,true,true,false,true,true));
    Check("false placed flag rejected", !Eligible(true,true,true,true,true,true,true,true,true,false));

    FreelancerCopyQueue::Queue queue;
    using FreelancerCopyQueue::EnqueueResult;
    Check("valid exact-root trigger request queues", queue.Enqueue(100,1,2,"assembly:/_pro/scenes/missions/snug/test",100)==EnqueueResult::Added);
    Check("duplicate pending request suppressed", queue.Enqueue(100,1,2,"assembly:/_pro/scenes/missions/snug/test",101)==EnqueueResult::Duplicate);
    Check("same item on another runtime root is a distinct request", queue.Enqueue(101,1,2,"assembly:/_pro/scenes/missions/snug/test",102)==EnqueueResult::Added);
    Check("different repository request accepted", queue.Enqueue(100,3,4,"assembly:/_pro/scenes/missions/snug/test",103)==EnqueueResult::Added);
    Check("fourth request fills bounded queue", queue.Enqueue(100,5,6,"s",104)==EnqueueResult::Added);
    Check("bounded queue rejects fifth distinct request", queue.Enqueue(100,7,8,"s",105)==EnqueueResult::Full);
    FreelancerCopyQueue::Request request;
    Check("oldest stable root, ID and scene are returned", queue.Pop(107,request) && request.rootInstanceToken==100 && request.repositoryHigh==1 && request.repositoryLow==2 && request.sceneKey[0]=='a');
    Check("same item is debounced at 149ms", queue.Enqueue(100,1,2,"assembly:/_pro/scenes/missions/snug/test",256)==EnqueueResult::Cooldown);
    Check("same item is allowed at exact 150ms boundary", queue.Enqueue(100,1,2,"assembly:/_pro/scenes/missions/snug/test",257)==EnqueueResult::Added);
    queue.Clear();
    Check("scene transition clears pending requests", queue.Size()==0);
    Check("empty root rejected", queue.Enqueue(0,0,0,"s",1400)==EnqueueResult::Invalid);
    Check("empty ID rejected", queue.Enqueue(100,0,0,"s",1400)==EnqueueResult::Invalid);
    FreelancerCopyQueue::Queue expiry;
    expiry.Enqueue(100,11,12,"s",100);
    Check("expired request is not popped", !expiry.Pop(5101,request));
    Check("empty scene rejected", expiry.Enqueue(100,11,12,"",5200)==EnqueueResult::Invalid);

    Check("copy scan is click-triggered in playing stage", FreelancerTreePolicy::ShouldTraverse(FreelancerTreePolicy::ScanTrigger::CopyClick,true));
    Check("copy scan is blocked outside playing stage", !FreelancerTreePolicy::ShouldTraverse(FreelancerTreePolicy::ScanTrigger::CopyClick,false));
    const auto copyNotice = FreelancerFeedbackPolicy::PromptMessageAlias(FreelancerFeedbackPolicy::Status::CopyRequested);
    const auto blockedNotice = FreelancerFeedbackPolicy::PromptMessageAlias(FreelancerFeedbackPolicy::Status::Blocked);
    const auto carriedNotice = FreelancerFeedbackPolicy::PromptMessageAlias(FreelancerFeedbackPolicy::Status::AlreadyCarried);
    const auto cooldownNotice = FreelancerFeedbackPolicy::PromptMessageAlias(FreelancerFeedbackPolicy::Status::Cooldown);
    const auto originalNotice = FreelancerFeedbackPolicy::PromptMessageAlias(FreelancerFeedbackPolicy::Status::OriginalTakeRequested);
    Check("prompt status aliases are present", std::string(copyNotice).size() && std::string(blockedNotice).size() &&
        std::string(carriedNotice).size() && std::string(cooldownNotice).size() && std::string(originalNotice).size());
    Check("prompt status aliases are distinct", std::string(copyNotice)!=blockedNotice && std::string(blockedNotice)!=carriedNotice &&
        std::string(carriedNotice)!=cooldownNotice && std::string(cooldownNotice)!=originalNotice && std::string(originalNotice)!=copyNotice);
    Check("hidden status has no prompt resource", std::string(FreelancerFeedbackPolicy::PromptMessageAlias(FreelancerFeedbackPolicy::Status::Hidden)).empty());
    Check("copy success requires a non-null creation handle",
        FreelancerFeedbackPolicy::ResolveCopyResult(true, false) == FreelancerFeedbackPolicy::Status::CopyRequested &&
        FreelancerFeedbackPolicy::ResolveCopyResult(false, false) == FreelancerFeedbackPolicy::Status::Blocked &&
        FreelancerFeedbackPolicy::ResolveCopyResult(false, true) == FreelancerFeedbackPolicy::Status::AlreadyCarried);
    FreelancerFeedbackPolicy::PromptFeedbackRoute copyRoute{
        FreelancerFeedbackPolicy::Status::CopyRequested, true, true, true, true, true, false,
        0x3ED7FB6D64794BFCULL, 0x31A63490ULL, 0x31A63490ULL};
    Check("prompt feedback accepts exact dynamic helper root with typed patch marker",
        FreelancerFeedbackPolicy::CanApplyPromptFeedback(copyRoute));
    Check("runtime helper entity ID may differ from its template ID",
        copyRoute.runtimeRootEntityId != 0xB0906E8ECB81A1B0ULL &&
        FreelancerFeedbackPolicy::CanApplyPromptFeedback(copyRoute));
    auto invalidPrompt = copyRoute;
    invalidPrompt.expectedRootInstanceToken = 0x06CB03369AAFB8E6ULL;
    Check("prompt feedback rejects a different runtime root token", !FreelancerFeedbackPolicy::CanApplyPromptFeedback(invalidPrompt));
    invalidPrompt = copyRoute;
    invalidPrompt.rootPresent = false;
    Check("prompt feedback rejects a missing root", !FreelancerFeedbackPolicy::CanApplyPromptFeedback(invalidPrompt));
    invalidPrompt = copyRoute;
    invalidPrompt.runtimeRootEntityId = 0;
    Check("prompt feedback rejects an invalid root identity", !FreelancerFeedbackPolicy::CanApplyPromptFeedback(invalidPrompt));
    invalidPrompt = copyRoute;
    invalidPrompt.patchMarkerPresent = false;
    Check("prompt feedback rejects a helper without the applied patch marker", !FreelancerFeedbackPolicy::CanApplyPromptFeedback(invalidPrompt));
    invalidPrompt = copyRoute;
    invalidPrompt.scenePlaying = false;
    Check("prompt feedback rejects nonplaying stage", !FreelancerFeedbackPolicy::CanApplyPromptFeedback(invalidPrompt));
    invalidPrompt = copyRoute;
    invalidPrompt.safehouseScene = false;
    Check("prompt feedback rejects non-safehouse scene", !FreelancerFeedbackPolicy::CanApplyPromptFeedback(invalidPrompt));
    invalidPrompt = copyRoute;
    invalidPrompt.sameScene = false;
    Check("prompt feedback rejects a changed scene", !FreelancerFeedbackPolicy::CanApplyPromptFeedback(invalidPrompt));
    invalidPrompt = copyRoute;
    invalidPrompt.hideNotifications = true;
    Check("Hide notifications blocks display without changing copy result",
        !FreelancerFeedbackPolicy::CanApplyPromptFeedback(invalidPrompt) &&
        FreelancerFeedbackPolicy::ResolveCopyResult(true, false) == FreelancerFeedbackPolicy::Status::CopyRequested);
    invalidPrompt = copyRoute;
    invalidPrompt.status = FreelancerFeedbackPolicy::Status::Hidden;
    Check("hidden feedback cannot change the prompt", !FreelancerFeedbackPolicy::CanApplyPromptFeedback(invalidPrompt));
    Check("rapid repeated status preserves original configured resource",
        FreelancerFeedbackPolicy::ResolvePromptBaseline(true,true,true,true,0x0033EDBB48F7D290ULL,0x006703A7B61641C9ULL)==0x0033EDBB48F7D290ULL);
    Check("different root snapshots its own configured resource",
        FreelancerFeedbackPolicy::ResolvePromptBaseline(true,false,true,true,0x0033EDBB48F7D290ULL,0x00EECBE80A262DCFULL)==0x00EECBE80A262DCFULL);
    Check("different scene snapshots current configured resource",
        FreelancerFeedbackPolicy::ResolvePromptBaseline(true,true,false,true,0x0033EDBB48F7D290ULL,0x00EECBE80A262DCFULL)==0x00EECBE80A262DCFULL);
    Check("a second root gets a separate feedback slot while the first awaits return",
        FreelancerFeedbackPolicy::ResolveSnapshotSlot(false,false,1,8)==FreelancerFeedbackPolicy::SnapshotSlotChoice::Append);
    Check("the same root updates its own pending prompt baseline",
        FreelancerFeedbackPolicy::ResolveSnapshotSlot(true,false,8,8)==FreelancerFeedbackPolicy::SnapshotSlotChoice::Existing);
    Check("cleared feedback slots are reused before the bounded ledger fills",
        FreelancerFeedbackPolicy::ResolveSnapshotSlot(false,true,8,8)==FreelancerFeedbackPolicy::SnapshotSlotChoice::ReuseFree);
    Check("feedback ledger refuses a ninth unrelated prompt snapshot",
        FreelancerFeedbackPolicy::ResolveSnapshotSlot(false,false,8,8)==FreelancerFeedbackPolicy::SnapshotSlotChoice::Full);
    Check("external prompt change becomes new restore baseline",
        FreelancerFeedbackPolicy::ResolvePromptBaseline(true,true,true,false,0x0033EDBB48F7D290ULL,0x00614EB9DAEF6CBEULL)==0x00614EB9DAEF6CBEULL);
    Check("feedback waits before expiry while selection and context remain valid",
        !FreelancerFeedbackPolicy::ShouldAttemptPromptRestore(true,false,false,false));
    Check("deferred prompt baselines do not scan while another root is selected",
        !FreelancerFeedbackPolicy::CanScanPromptForRestore(true,true,false,false));
    Check("deferred prompt baseline becomes eligible only when its same root is selected",
        FreelancerFeedbackPolicy::CanScanPromptForRestore(true,true,true,false) &&
        FreelancerFeedbackPolicy::ShouldAttemptPromptRestore(true,false,true,false));
    Check("an ineligible selected root waits for a selection cycle instead of rescanning repeatedly",
        !FreelancerFeedbackPolicy::CanScanPromptForRestore(true,true,true,true));
    Check("feedback restores at timeout", FreelancerFeedbackPolicy::ShouldAttemptPromptRestore(true,true,false,false));
    Check("feedback restores before another wall selection", FreelancerFeedbackPolicy::ShouldAttemptPromptRestore(true,false,true,false));
    Check("feedback restores when focus or pause context is lost", FreelancerFeedbackPolicy::ShouldAttemptPromptRestore(true,false,false,true));
    Check("restoration requires same live scene, patch, root token, and unchanged feedback text",
        FreelancerFeedbackPolicy::CanRestorePromptFeedback(true,true,true,true,true,true,true));
    Check("restoration preserves a user or option change to prompt text",
        !FreelancerFeedbackPolicy::CanRestorePromptFeedback(true,true,true,true,true,true,false));
    Check("restoration rejects a stale runtime root token",
        !FreelancerFeedbackPolicy::CanRestorePromptFeedback(true,true,true,true,true,false,true));
    Check("restoration rejects a scene transition before engine writes",
        !FreelancerFeedbackPolicy::CanRestorePromptFeedback(true,true,false,true,true,true,true));
    Check("restoration rejects removed or unpatched roots",
        !FreelancerFeedbackPolicy::CanRestorePromptFeedback(true,true,true,false,true,true,true) &&
        !FreelancerFeedbackPolicy::CanRestorePromptFeedback(true,true,true,true,false,true,true));
    Check("P original take requires exact patch selection and current eligibility", CanTriggerOriginalTake(true,true,true,true,true,true,true,true,true));
    Check("Default Keybinds ON maps P to copy", ResolvePAction(true) == PAction::Copy);
    Check("Default Keybinds OFF maps P to original take", ResolvePAction(false) == PAction::OriginalTake);
    Check("P copy in Default Keybinds mode still requires the exact patch marker",
        !CanTriggerPAction(true,true,true,true,true,true,true,false,true));
    Check("P copy in Default Keybinds mode still requires the focused unpaused selection gate",
        !CanTriggerPAction(true,true,false,true,false,true,true,true,true));
    Check("P original take uses the same exact patch and eligibility gates",
        CanTriggerPAction(true,true,true,true,true,true,true,true,true));
    Check("P is blocked when the option patch marker is absent", !CanTriggerOriginalTake(true,true,true,true,true,true,true,false,true));
    Check("P is blocked while paused or outside focused gameplay", !CanTriggerOriginalTake(true,true,true,true,false,true,true,true,true));
    Check("P is blocked outside game foreground", !CanTriggerOriginalTake(true,true,false,true,true,true,true,true,true));
    Check("P is blocked when SDK input is disabled", !CanTriggerOriginalTake(true,true,true,false,true,true,true,true,true));
    Check("P is blocked without current exact selection", !CanTriggerOriginalTake(true,true,true,true,true,false,true,true,true));
    Check("P is blocked when current wall scan fails", !CanTriggerOriginalTake(true,true,true,true,true,true,true,true,false));
    std::printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}

