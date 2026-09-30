#include "cpu/o3/klp_timing_stats.hh"

#include <algorithm>
#include <cassert>

#include "cpu/o3/cpu.hh"
#include "cpu/o3/dyn_inst.hh"

namespace gem5
{
namespace o3
{

KlpTimingStats::KlpTimingStats(CPU *_cpu)
    : statistics::Group(_cpu, "klpTiming"),
      ADD_STAT(grants, statistics::units::Count::get(),
               "KLP unconditional grants observed in this statistics epoch"),
      ADD_STAT(grantsHeld, statistics::units::Count::get(),
               "Grants while complete load data was held"),
      ADD_STAT(grantsStlfBlocked, statistics::units::Count::get(),
               "Grants while STLF was blocked by its key"),
      ADD_STAT(grantsOther, statistics::units::Count::get(),
               "Grants before data hold or STLF key blocking"),
      ADD_STAT(grantsWithoutBranchPair, statistics::units::Count::get(),
               "Grants without an older branch retired in this epoch"),
      ADD_STAT(broadcasts, statistics::units::Count::get(),
               "Nonfaulted nonsquashed KLP loads reaching real IEW broadcast"),
      ADD_STAT(broadcastsWithoutGrantPair, statistics::units::Count::get(),
               "Broadcast loads without a grant observed in this epoch"),
      ADD_STAT(grantBroadcastPairs, statistics::units::Count::get(),
               "Load grant and real broadcast paired within one epoch"),
      ADD_STAT(branchGrantBroadcastPairs, statistics::units::Count::get(),
               "Older branch retire, load grant and broadcast paired"),
      ADD_STAT(retiredGrantBroadcastPairs, statistics::units::Count::get(),
               "Grant-broadcast pairs whose loads also retired this epoch"),
      ADD_STAT(retiredBranchGrantBroadcastPairs,
               statistics::units::Count::get(),
               "Branch-grant-broadcast pairs also retired this epoch"),
      ADD_STAT(stlfRetryBroadcastPairs, statistics::units::Count::get(),
               "Last key-blocked STLF retry and broadcast paired this epoch"),
      ADD_STAT(stlfFirstBlockBroadcastPairs, statistics::units::Count::get(),
               "First STLF key block and broadcast paired this epoch"),
      ADD_STAT(retiredStlfRetryBroadcastPairs, statistics::units::Count::get(),
               "STLF retry-broadcast pairs also retired this epoch"),
      ADD_STAT(retiredStlfFirstBlockBroadcastPairs,
               statistics::units::Count::get(),
               "First STLF block-broadcast pairs also retired this epoch"),
      ADD_STAT(memOrderRecoveryHandled, statistics::units::Count::get(),
               "Memory-order recovery requests emitted by IEW; may later be superseded"),
      ADD_STAT(memOrderRecoverySuppressed, statistics::units::Count::get(),
               "Memory-order recovery requests suppressed by an older recovery"),
      ADD_STAT(branchRetireToGrant, statistics::units::Cycle::get(),
               "Older branch retire to grant, same-epoch broadcast population"),
      ADD_STAT(branchRetireToGrantHeld, statistics::units::Cycle::get(),
               "Branch retire to grant for data-held loads at grant"),
      ADD_STAT(branchRetireToGrantStlfBlocked, statistics::units::Cycle::get(),
               "Branch retire to grant for STLF-key-blocked loads at grant"),
      ADD_STAT(branchRetireToGrantOther, statistics::units::Cycle::get(),
               "Branch retire to grant for other loads at grant"),
      ADD_STAT(grantToBroadcast, statistics::units::Cycle::get(),
               "Grant to real IEW broadcast, same-epoch paired loads"),
      ADD_STAT(grantToBroadcastHeld, statistics::units::Cycle::get(),
               "Grant to broadcast for data-held loads at grant"),
      ADD_STAT(grantToBroadcastStlfBlocked, statistics::units::Cycle::get(),
               "Grant to broadcast for STLF-key-blocked loads at grant"),
      ADD_STAT(grantToBroadcastOther, statistics::units::Cycle::get(),
               "Grant to broadcast for other loads at grant"),
      ADD_STAT(branchRetireToBroadcast, statistics::units::Cycle::get(),
               "Older branch retire to real IEW broadcast"),
      ADD_STAT(heldToBranchRetire, statistics::units::Cycle::get(),
               "max(0, branch retire minus data hold) for paired held loads"),
      ADD_STAT(heldToGrant, statistics::units::Cycle::get(),
               "Data hold to grant, same sample gate as held wait decomposition"),
      ADD_STAT(heldAfterBranchRetireToGrant, statistics::units::Cycle::get(),
               "Grant minus max(branch retire, data hold) for paired held loads"),
      ADD_STAT(stlfBlockedToBranchRetire, statistics::units::Cycle::get(),
               "max(0, branch retire minus STLF block) for paired blocked loads"),
      ADD_STAT(stlfBlockedToGrant, statistics::units::Cycle::get(),
               "STLF block to grant, same sample gate as blocked wait decomposition"),
      ADD_STAT(stlfBlockedAfterBranchRetireToGrant,
               statistics::units::Cycle::get(),
               "Grant minus max(branch retire, STLF block) for paired blocked loads"),
      ADD_STAT(dataReadyToBroadcast, statistics::units::Cycle::get(),
               "Complete data ready to real broadcast, observed same epoch"),
      ADD_STAT(releaseToBroadcast, statistics::units::Cycle::get(),
               "LQ release to real broadcast, observed same epoch"),
      ADD_STAT(stlfRetryToBroadcast, statistics::units::Cycle::get(),
               "Last key-blocked STLF retry to real broadcast"),
      ADD_STAT(stlfRetryToBroadcastUncondi, statistics::units::Cycle::get(),
               "STLF retry caused by uncondi to real broadcast"),
      ADD_STAT(stlfRetryToBroadcastStoreComplete,
               statistics::units::Cycle::get(),
               "STLF retry caused by store completion to real broadcast"),
      ADD_STAT(stlfFirstBlockToBroadcast, statistics::units::Cycle::get(),
               "First STLF key block to real broadcast"),
      ADD_STAT(retiredBranchRetireToGrant, statistics::units::Cycle::get(),
               "Branch retire to grant for loads retired in the same epoch"),
      ADD_STAT(retiredGrantToBroadcast, statistics::units::Cycle::get(),
               "Grant to broadcast for loads retired in the same epoch"),
      ADD_STAT(retiredStlfRetryToBroadcast, statistics::units::Cycle::get(),
               "Last STLF retry to broadcast for loads retired this epoch"),
      ADD_STAT(retiredStlfFirstBlockToBroadcast,
               statistics::units::Cycle::get(),
               "First STLF key block to broadcast for loads retired this epoch"),
      cpu(_cpu)
{
    for (auto *dist : {
            &branchRetireToGrant, &branchRetireToGrantHeld,
            &branchRetireToGrantStlfBlocked, &branchRetireToGrantOther,
            &grantToBroadcast, &grantToBroadcastHeld,
            &grantToBroadcastStlfBlocked, &grantToBroadcastOther,
            &branchRetireToBroadcast, &heldToBranchRetire, &heldToGrant,
            &heldAfterBranchRetireToGrant, &stlfBlockedToBranchRetire,
            &stlfBlockedToGrant,
            &stlfBlockedAfterBranchRetireToGrant,
            &dataReadyToBroadcast, &releaseToBroadcast,
            &stlfRetryToBroadcast, &stlfRetryToBroadcastUncondi,
            &stlfRetryToBroadcastStoreComplete, &stlfFirstBlockToBroadcast,
            &retiredBranchRetireToGrant, &retiredGrantToBroadcast,
            &retiredStlfRetryToBroadcast, &retiredStlfFirstBlockToBroadcast}) {
        dist->init(0, 255, 1).flags(statistics::nozero);
    }
}

void
KlpTimingStats::resetStats()
{
    statistics::Group::resetStats();
    ++currentEpoch;
    // Never pair a post-reset grant with a pre-reset branch retirement.
    retiredBranches = {};
}

KlpTimingState &
KlpTimingStats::state(DynInst &inst)
{
    if (inst.klpTiming.epoch != currentEpoch) {
        inst.klpTiming = {};
        inst.klpTiming.epoch = currentEpoch;
    }
    return inst.klpTiming;
}

void
KlpTimingStats::recordBranchRetire(const DynInst &inst)
{
    if (!cpu->isKlpEnabled() || !inst.isControl() || inst.isSquashed() ||
        inst.getFault() != NoFault)
        return;
    assert(inst.threadNumber < retiredBranches.size());
    retiredBranches[inst.threadNumber] =
        {true, inst.seqNum, cpu->curCycle()};
}

void
KlpTimingStats::recordGrant(DynInst &inst)
{
    if (!inst.isKlpLoad() || inst.isSquashed() || inst.getFault() != NoFault)
        return;
    auto &d = state(inst);
    if (d.grantValid)
        return;
    d.grantValid = true;
    d.grantCycle = cpu->curCycle();
    d.grantHeld = inst.klpWaitingForAuthorization;
    d.grantStlfBlocked = !d.grantHeld && inst.klpStlfBlocked;
    d.grantHeldStartValid = d.grantHeld && d.cacheHoldValid;
    d.grantHeldStartCycle = d.cacheHoldCycle;
    d.grantStlfStartValid = d.grantStlfBlocked && d.stlfFirstBlockValid;
    d.grantStlfStartCycle = d.stlfBlockStartCycle;
    ++grants;
    if (d.grantHeld)
        ++grantsHeld;
    else if (d.grantStlfBlocked)
        ++grantsStlfBlocked;
    else
        ++grantsOther;
    const auto &branch = retiredBranches[inst.threadNumber];
    if (branch.valid && branch.seq < inst.seqNum) {
        d.branchPairValid = true;
        d.branchSeq = branch.seq;
        d.branchRetireCycle = branch.cycle;
    } else {
        ++grantsWithoutBranchPair;
    }
}

void
KlpTimingStats::recordBroadcast(DynInst &inst)
{
    if (!inst.isKlpLoad() || inst.isSquashed() ||
        !inst.isExecuted() || inst.getFault() != NoFault)
        return;
    auto &d = state(inst);
    if (d.broadcastRecorded)
        return;
    d.broadcastRecorded = true;
    d.broadcastCycle = cpu->curCycle();
    const Cycles now = d.broadcastCycle;
    ++broadcasts;
    if (d.grantValid && now >= d.grantCycle) {
        ++grantBroadcastPairs;
        const uint64_t delay = static_cast<uint64_t>(now - d.grantCycle);
        grantToBroadcast.sample(delay);
        if (d.grantHeld)
            grantToBroadcastHeld.sample(delay);
        else if (d.grantStlfBlocked)
            grantToBroadcastStlfBlocked.sample(delay);
        else
            grantToBroadcastOther.sample(delay);
        if (d.branchPairValid && d.grantCycle >= d.branchRetireCycle) {
            ++branchGrantBroadcastPairs;
            const uint64_t branch_delay = static_cast<uint64_t>(
                d.grantCycle - d.branchRetireCycle);
            branchRetireToGrant.sample(branch_delay);
            branchRetireToBroadcast.sample(static_cast<uint64_t>(
                now - d.branchRetireCycle));
            if (d.grantHeld) {
                branchRetireToGrantHeld.sample(branch_delay);
                if (d.grantHeldStartValid &&
                    d.grantHeldStartCycle <= d.grantCycle) {
                    const Cycles split = std::max(d.branchRetireCycle,
                                                  d.grantHeldStartCycle);
                    heldToBranchRetire.sample(static_cast<uint64_t>(
                        split - d.grantHeldStartCycle));
                    heldToGrant.sample(static_cast<uint64_t>(
                        d.grantCycle - d.grantHeldStartCycle));
                    heldAfterBranchRetireToGrant.sample(static_cast<uint64_t>(
                        d.grantCycle - std::max(d.branchRetireCycle,
                                              d.grantHeldStartCycle)));
                }
            } else if (d.grantStlfBlocked) {
                branchRetireToGrantStlfBlocked.sample(branch_delay);
                if (d.grantStlfStartValid &&
                    d.grantStlfStartCycle <= d.grantCycle) {
                    const Cycles split = std::max(d.branchRetireCycle,
                                                  d.grantStlfStartCycle);
                    stlfBlockedToBranchRetire.sample(static_cast<uint64_t>(
                        split - d.grantStlfStartCycle));
                    stlfBlockedToGrant.sample(static_cast<uint64_t>(
                        d.grantCycle - d.grantStlfStartCycle));
                    stlfBlockedAfterBranchRetireToGrant.sample(
                        static_cast<uint64_t>(d.grantCycle -
                            std::max(d.branchRetireCycle,
                                     d.grantStlfStartCycle)));
                }
            } else {
                branchRetireToGrantOther.sample(branch_delay);
            }
        }
    } else {
        ++broadcastsWithoutGrantPair;
    }
    if (d.dataReadyValid && now >= d.dataReadyCycle)
        dataReadyToBroadcast.sample(static_cast<uint64_t>(now - d.dataReadyCycle));
    if (d.releaseValid && now >= d.releaseCycle)
        releaseToBroadcast.sample(static_cast<uint64_t>(now - d.releaseCycle));
    if (d.stlfLastRetryValid && now >= d.stlfLastRetryCycle) {
        ++stlfRetryBroadcastPairs;
        const uint64_t delay = static_cast<uint64_t>(
            now - d.stlfLastRetryCycle);
        stlfRetryToBroadcast.sample(delay);
        if (d.stlfLastRetryReason == KlpRetryReason::Uncondi)
            stlfRetryToBroadcastUncondi.sample(delay);
        else if (d.stlfLastRetryReason == KlpRetryReason::StoreComplete)
            stlfRetryToBroadcastStoreComplete.sample(delay);
    }
    if (d.stlfFirstBlockValid && now >= d.stlfFirstBlockCycle) {
        ++stlfFirstBlockBroadcastPairs;
        stlfFirstBlockToBroadcast.sample(static_cast<uint64_t>(
            now - d.stlfFirstBlockCycle));
    }
}

void
KlpTimingStats::recordRetirement(DynInst &inst)
{
    if (!inst.isKlpLoad() || inst.isSquashed() || inst.getFault() != NoFault)
        return;
    auto &d = state(inst);
    if (!d.broadcastRecorded || d.retiredRecorded)
        return;
    d.retiredRecorded = true;
    const Cycles now = d.broadcastCycle;
    if (d.grantValid && now >= d.grantCycle) {
        ++retiredGrantBroadcastPairs;
        retiredGrantToBroadcast.sample(static_cast<uint64_t>(
            now - d.grantCycle));
        if (d.branchPairValid && d.grantCycle >= d.branchRetireCycle) {
            ++retiredBranchGrantBroadcastPairs;
            retiredBranchRetireToGrant.sample(static_cast<uint64_t>(
                d.grantCycle - d.branchRetireCycle));
        }
    }
    if (d.stlfLastRetryValid && now >= d.stlfLastRetryCycle) {
        ++retiredStlfRetryBroadcastPairs;
        retiredStlfRetryToBroadcast.sample(static_cast<uint64_t>(
            now - d.stlfLastRetryCycle));
    }
    if (d.stlfFirstBlockValid && now >= d.stlfFirstBlockCycle) {
        ++retiredStlfFirstBlockBroadcastPairs;
        retiredStlfFirstBlockToBroadcast.sample(static_cast<uint64_t>(
            now - d.stlfFirstBlockCycle));
    }
}

} // namespace o3
} // namespace gem5
