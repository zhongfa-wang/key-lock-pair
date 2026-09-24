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

/** Bounded, observational diagnostics; never participates in authorization. */
class KlpProvStats : public statistics::Group
{
  public:
    enum class LoadPhase { Check, Hold, StlfBlocked };
    static constexpr unsigned MaxPcEntries = 2048;

    explicit KlpProvStats(statistics::Group *parent);
    void resetStats() override;

    // Call once at the requested observation point. This class does not
    // deduplicate dynamic instructions, retries, or STLF attempts.
    void recordLoad(Addr pc, const AddrProv &prov, LoadPhase phase);
    void recordLoad(Addr pc, const AddrProv &prov, bool held)
    {
        recordLoad(pc, prov, held ? LoadPhase::Hold : LoadPhase::Check);
    }
    void recordProducer(Addr pc, const char *mnemonic, const AddrProv &prov,
                        bool fresh, AddrProvOrigin step_reason);
    void recordScaled(const AddrProv &index, const AddrProv &base,
                      const AddrProv &result, RegVal index_value,
                      RegVal base_value, unsigned xlen);

    void beginTakeover() { takingOver = true; }
    void endTakeover() { takingOver = false; }
    bool inTakeover() const { return takingOver; }

  private:
    using LoadKey = std::tuple<Addr, AddrProvOrigin, Addr>;
    using ProducerKey = std::tuple<Addr, std::string, AddrProvOrigin, bool>;
    std::map<LoadKey, unsigned> loadKeys;
    std::map<ProducerKey, unsigned> producerKeys;
    bool takingOver = false;

    unsigned loadSlot(Addr pc, const AddrProv &prov);
    unsigned producerSlot(Addr pc, const char *mnemonic,
                          AddrProvOrigin reason, bool fresh);
    void resetPcLabels();

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
    statistics::Vector scaledInputStates;
    statistics::Vector scaledResults;
    statistics::Vector scaledMagnitude;
    statistics::Vector scaledUnknownMagnitude;
    statistics::Vector scaledUnknownMagnitudeByState;
};

} // namespace o3
} // namespace gem5

#endif // __CPU_O3_KLP_PROV_STATS_HH__
