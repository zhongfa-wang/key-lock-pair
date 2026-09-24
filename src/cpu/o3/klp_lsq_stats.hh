#ifndef __CPU_O3_KLP_LSQ_STATS_HH__
#define __CPU_O3_KLP_LSQ_STATS_HH__

#include <map>
#include <string>

#include "base/statistics.hh"
#include "base/types.hh"

namespace gem5 { namespace o3 {

/** Observations only: none of these counters participate in scheduling. */
struct KlpLsqStats : public statistics::Group
{
    explicit KlpLsqStats(statistics::Group *parent);
    void resetStats() override;
    void recordStlfPair(Addr load, Addr store, unsigned reason);
    void recordViolation(Addr trigger, Addr victim, bool load_trigger,
                         bool byte_overlap, unsigned victim_flags);

    statistics::Vector checkOutcome;
    statistics::Vector heldReason;
    statistics::Vector stlfBlockReason;
    statistics::Scalar stlfBlockedLoads;
    statistics::Vector stlfWakeReason;
    statistics::Histogram stlfBlockToWake;
    statistics::Histogram stlfBlockToSquash;
    statistics::Histogram stlfRetryToRead;
    statistics::Scalar stlfReadsAfterRetry;
    statistics::Vector stlfRetryOutcome;
    statistics::Vector stlfPairs;
    statistics::Vector violationKind;
    statistics::Vector violationVictimFlags;
    statistics::Vector violationPairs;
    statistics::Scalar snoopReexec;
    statistics::Scalar snoopReexecHeld;
    statistics::Scalar squashedHeld;
    statistics::Histogram heldToSquash;
    statistics::Scalar squashedAfterStlfBlock;

  private:
    static constexpr unsigned PairLimit = 4096;
    std::map<std::string, unsigned> stlfPairIds;
    std::map<std::string, unsigned> violationPairIds;
    void samplePair(statistics::Vector &stat,
                    std::map<std::string, unsigned> &ids,
                    const std::string &key);
};

} } // namespace gem5::o3
#endif
