#ifndef __CPU_O3_KLP_PROV_STATS_HH__
#define __CPU_O3_KLP_PROV_STATS_HH__

#include <map>
#include <string>
#include <tuple>

#include "base/statistics.hh"
#include "cpu/addr_prov.hh"

namespace gem5
{
namespace o3
{

/** Per-dynamic-load accounting, retained across retries and stats resets. */
struct KlpThresholdLoadState
{
    uint64_t firstExecutionEpoch = 0;
    bool stalled = false;

    bool noteExecution(uint64_t epoch)
    {
        if (firstExecutionEpoch)
            return false;
        firstExecutionEpoch = epoch;
        return true;
    }

    bool noteStall(uint64_t epoch, const AddrProv &prov)
    {
        // A load begun before reset is outside the new denominator cohort.
        if (firstExecutionEpoch != epoch || stalled ||
            prov.state != AddrProv::State::AMBIGUOUS ||
            !prov.thresholdIntroducedAmbiguity)
            return false;
        stalled = true;
        return true;
    }
};

/** Bounded, observational diagnostics; never participates in authorization. */
class KlpProvStats : public statistics::Group
{
  public:
    enum class LoadPhase { Check, Hold, StlfBlocked };
    static constexpr unsigned MaxPcEntries = 2048;

    explicit KlpProvStats(statistics::Group *parent);
    void resetStats() override;
    void preDumpStats() override;
    // Sample the address-source provenance on the first execution attempt,
    // using the same dynamic-load cohort as executedLoads.
    void recordLoadExecution(KlpThresholdLoadState &state, const AddrProv &prov);
    void recordThresholdStall(KlpThresholdLoadState &state, const AddrProv &prov);

    // Call once at the requested observation point. This class does not
    // deduplicate dynamic instructions, retries, or STLF attempts.
    void recordLoad(Addr pc, const AddrProv &prov, LoadPhase phase);
    void recordLoad(Addr pc, const AddrProv &prov, bool held)
    {
        recordLoad(pc, prov, held ? LoadPhase::Hold : LoadPhase::Check);
    }
    void recordProducer(Addr pc, const char *mnemonic, const AddrProv &prov,
                        bool fresh, AddrProvOrigin step_reason);
    void recordComparison(const AddrProvCompare &comparison);
    void recordScaled(const AddrProv &index, const AddrProv &base,
                      const AddrProv &result,
                      const AddrProvCompare &comparison);

    void beginTakeover() { takingOver = true; }
    void endTakeover() { takingOver = false; }
    bool inTakeover() const { return takingOver; }

  private:
    using LoadKey = std::tuple<Addr, AddrProvOrigin, Addr>;
    using ProducerKey = std::tuple<Addr, std::string, AddrProvOrigin, bool>;
    std::map<LoadKey, unsigned> loadKeys;
    std::map<ProducerKey, unsigned> producerKeys;
    bool takingOver = false;
    uint64_t loadEpoch = 1;

    unsigned loadSlot(Addr pc, const AddrProv &prov);
    unsigned producerSlot(Addr pc, const char *mnemonic,
                          AddrProvOrigin reason, bool fresh);
    void resetPcLabels();

    statistics::Scalar executedLoads;
    statistics::Vector executedLoadStates;
    // Derived at dump time so an empty epoch produces finite zero fractions.
    statistics::Vector executedLoadStateFractions;
    statistics::Scalar thresholdStalledLoads;
    statistics::Value thresholdStalledLoadFraction;
    statistics::Vector checkStates;
    statistics::Vector holdStates;
    statistics::Vector stlfBlockedStates;
    statistics::Vector checkOrigins;
    statistics::Vector holdOrigins;
    statistics::Vector stlfBlockedOrigins;
    statistics::Vector unknownChecksByPcOrigin;
    statistics::Vector unknownHoldsByPcOrigin;
    statistics::Vector unknownStlfByPcOrigin;
    statistics::Vector freshOrigins;
    statistics::Vector propagatedOrigins;
    statistics::Vector unknownProducerByPc;
    statistics::Vector inputCompareReasons;
    statistics::Vector inputCompareSelections;
    statistics::Vector scaledInputStates;
    statistics::Vector scaledResults;
    statistics::Vector scaledComparison;
    statistics::Vector scaledUnknownComparison;
    statistics::Vector scaledUnknownComparisonByState;
};

} // namespace o3
} // namespace gem5

#endif // __CPU_O3_KLP_PROV_STATS_HH__
