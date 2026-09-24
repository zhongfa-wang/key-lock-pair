#ifndef __CPU_O3_KLP_TIMING_STATS_HH__
#define __CPU_O3_KLP_TIMING_STATS_HH__

#include <array>
#include <cstdint>

#include "base/statistics.hh"
#include "base/types.hh"
#include "cpu/inst_seq.hh"
#include "cpu/o3/limits.hh"

namespace gem5
{
namespace o3
{

class CPU;
class DynInst;

enum class KlpRetryReason { None, Uncondi, StoreComplete };

/** Diagnostic timestamps only: none of these fields drive execution. */
struct KlpTimingState
{
    uint64_t epoch = 0;
    bool grantValid = false;
    Cycles grantCycle = Cycles(0);
    bool grantHeld = false;
    bool grantStlfBlocked = false;
    bool grantHeldStartValid = false;
    Cycles grantHeldStartCycle = Cycles(0);
    bool grantStlfStartValid = false;
    Cycles grantStlfStartCycle = Cycles(0);
    bool branchPairValid = false;
    InstSeqNum branchSeq = 0;
    Cycles branchRetireCycle = Cycles(0);
    bool broadcastRecorded = false;
    Cycles broadcastCycle = Cycles(0);
    bool retiredRecorded = false;

    bool stlfFirstBlockValid = false;
    Cycles stlfFirstBlockCycle = Cycles(0);
    uint64_t stlfBlockCount = 0;
    bool stlfLastRetryValid = false;
    Cycles stlfLastRetryCycle = Cycles(0);
    KlpRetryReason stlfLastRetryReason = KlpRetryReason::None;
    bool stlfBlockActive = false;
    Cycles stlfBlockStartCycle = Cycles(0);
    bool stlfRetryPending = false;
    unsigned stlfReadAttempts = 0;
    uint64_t stlfBlockCycles = 0;
    bool stlfEverBlocked = false;

    // 0 pass, 1 NONE, 2 ambiguous, 3 invalid, 4 mismatch, 5 both.
    unsigned lastCheckOutcome = 0;
    bool cacheHoldValid = false;
    Cycles cacheHoldCycle = Cycles(0);
    bool dataReadyValid = false;
    Cycles dataReadyCycle = Cycles(0);
    bool releaseValid = false;
    Cycles releaseCycle = Cycles(0);
};

/** Same-epoch, explicitly paired KLP timing observations. */
class KlpTimingStats : public statistics::Group
{
  public:
    explicit KlpTimingStats(CPU *cpu);
    void resetStats() override;
    KlpTimingState &state(DynInst &inst);
    uint64_t epoch() const { return currentEpoch; }
    void recordBranchRetire(const DynInst &inst);
    void recordGrant(DynInst &inst);
    void recordBroadcast(DynInst &inst);
    void recordRetirement(DynInst &inst);

    statistics::Scalar grants;
    statistics::Scalar grantsHeld;
    statistics::Scalar grantsStlfBlocked;
    statistics::Scalar grantsOther;
    statistics::Scalar grantsWithoutBranchPair;
    statistics::Scalar broadcasts;
    statistics::Scalar broadcastsWithoutGrantPair;
    statistics::Scalar grantBroadcastPairs;
    statistics::Scalar branchGrantBroadcastPairs;
    statistics::Scalar retiredGrantBroadcastPairs;
    statistics::Scalar retiredBranchGrantBroadcastPairs;
    statistics::Scalar stlfRetryBroadcastPairs;
    statistics::Scalar stlfFirstBlockBroadcastPairs;
    statistics::Scalar retiredStlfRetryBroadcastPairs;
    statistics::Scalar retiredStlfFirstBlockBroadcastPairs;
    statistics::Scalar memOrderRecoveryHandled;
    statistics::Scalar memOrderRecoverySuppressed;

    statistics::Distribution branchRetireToGrant;
    statistics::Distribution branchRetireToGrantHeld;
    statistics::Distribution branchRetireToGrantStlfBlocked;
    statistics::Distribution branchRetireToGrantOther;
    statistics::Distribution grantToBroadcast;
    statistics::Distribution grantToBroadcastHeld;
    statistics::Distribution grantToBroadcastStlfBlocked;
    statistics::Distribution grantToBroadcastOther;
    statistics::Distribution branchRetireToBroadcast;
    statistics::Distribution heldToBranchRetire;
    statistics::Distribution heldToGrant;
    statistics::Distribution heldAfterBranchRetireToGrant;
    statistics::Distribution stlfBlockedToBranchRetire;
    statistics::Distribution stlfBlockedToGrant;
    statistics::Distribution stlfBlockedAfterBranchRetireToGrant;
    statistics::Distribution dataReadyToBroadcast;
    statistics::Distribution releaseToBroadcast;
    statistics::Distribution stlfRetryToBroadcast;
    statistics::Distribution stlfRetryToBroadcastUncondi;
    statistics::Distribution stlfRetryToBroadcastStoreComplete;
    statistics::Distribution stlfFirstBlockToBroadcast;
    statistics::Distribution retiredBranchRetireToGrant;
    statistics::Distribution retiredGrantToBroadcast;
    statistics::Distribution retiredStlfRetryToBroadcast;
    statistics::Distribution retiredStlfFirstBlockToBroadcast;

  private:
    struct RetiredBranch
    {
        bool valid = false;
        InstSeqNum seq = 0;
        Cycles cycle = Cycles(0);
    };
    CPU *cpu;
    uint64_t currentEpoch = 1;
    std::array<RetiredBranch, MaxThreads> retiredBranches{};
};

} // namespace o3
} // namespace gem5

#endif
