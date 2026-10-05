#pragma once

namespace FreelancerTreePolicy {
enum class ScanTrigger { CopyClick };
constexpr bool ShouldTraverse(ScanTrigger trigger, bool scenePlaying) {
    return trigger == ScanTrigger::CopyClick && scenePlaying;
}
}
