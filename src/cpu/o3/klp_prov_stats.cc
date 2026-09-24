#include "cpu/o3/klp_prov_stats.hh"

#include <array>
#include <sstream>

namespace gem5
{
namespace o3
{

namespace
{
constexpr unsigned NumOrigins =
    static_cast<unsigned>(AddrProvOrigin::NumOrigins);
constexpr std::array<const char *, NumOrigins> OriginNames = {
    "untracked", "architectural_reset", "cpu_takeover", "unsupported",
    "fault_or_predicate", "memory_pending", "zero_register", "magnitude_tie",
    "logical_tie", "scaled_base_selected", "scaled_index_dominant",
    "scaled_magnitude_tie",
    "arithmetic_selected", "strong_seed", "weak_seed", "constant_seed",
    "load_seed"
};
constexpr std::array<const char *, 4> StateNames = {
    "none", "weak", "strong", "ambiguous"
};
constexpr std::array<const char *, 3> MagnitudeNames = {
    "base_larger", "index_larger", "tie"
};

unsigned
originIndex(AddrProvOrigin origin)
{
    const unsigned index = static_cast<unsigned>(origin);
    return index < NumOrigins ? index : 0;
}

bool
unavailable(const AddrProv &prov)
{
    return prov.state == AddrProv::State::NONE ||
           prov.state == AddrProv::State::AMBIGUOUS;
}
} // anonymous namespace

KlpProvStats::KlpProvStats(statistics::Group *parent)
    : statistics::Group(parent, "klpProv"),
      ADD_STAT(checkStates, statistics::units::Count::get(),
               "EA provenance state at the first complete cache check"),
      ADD_STAT(holdStates, statistics::units::Count::get(),
               "EA provenance state on entering a KLP data hold"),
      ADD_STAT(stlfBlockedStates, statistics::units::Count::get(),
               "EA provenance state on entering a key-blocked STLF wait"),
      ADD_STAT(checkOrigins, statistics::units::Count::get(),
               "EA diagnostic origin at the first complete cache check"),
      ADD_STAT(holdOrigins, statistics::units::Count::get(),
               "EA diagnostic origin on entering a KLP data hold"),
      ADD_STAT(stlfBlockedOrigins, statistics::units::Count::get(),
               "EA diagnostic origin on entering a key-blocked STLF wait"),
      ADD_STAT(unknownChecksByPcOrigin, statistics::units::Count::get(),
               "Unknown checks by load PC and first origin; bounded table"),
      ADD_STAT(unknownHoldsByPcOrigin, statistics::units::Count::get(),
               "Unknown holds by load PC and first origin; bounded table"),
      ADD_STAT(unknownStlfByPcOrigin, statistics::units::Count::get(),
               "Unknown STLF waits by load PC and first origin; bounded table"),
      ADD_STAT(freshOrigins, statistics::units::Count::get(),
               "Fresh diagnostic origins produced by executed instructions"),
      ADD_STAT(propagatedOrigins, statistics::units::Count::get(),
               "Existing diagnostic origins carried by executed instructions"),
      ADD_STAT(unknownProducerByPc, statistics::units::Count::get(),
               "Unknown-result producer PC/opcode/reason; bounded table"),
      ADD_STAT(scaledInputStates, statistics::units::Count::get(),
               "Scaled-index-base input states, index then unscaled base"),
      ADD_STAT(scaledResults, statistics::units::Count::get(),
               "Scaled comparison results; NONE and inherited bins retained "
               "for historical comparisons and zero under the current rule"),
      ADD_STAT(scaledMagnitude, statistics::units::Count::get(),
               "Raw full-XLEN magnitude outcomes for all shNadd comparisons"),
      ADD_STAT(scaledUnknownMagnitude, statistics::units::Count::get(),
               "Raw operand comparison for unknown shNadd outputs; "
               "base_larger is zero under the current directional rule"),
      ADD_STAT(scaledUnknownMagnitudeByState, statistics::units::Count::get(),
               "Unknown shNadd comparison outcomes by input states")
{
    for (auto *stat : {&checkStates, &holdStates, &stlfBlockedStates}) {
        stat->init(StateNames.size());
        stat->flags(statistics::total | statistics::nozero);
        for (unsigned i = 0; i < StateNames.size(); ++i)
            stat->subname(i, StateNames[i]);
    }
    for (auto *stat : {&checkOrigins, &holdOrigins, &stlfBlockedOrigins,
                       &freshOrigins, &propagatedOrigins}) {
        stat->init(NumOrigins);
        stat->flags(statistics::total | statistics::nozero);
        for (unsigned i = 0; i < NumOrigins; ++i)
            stat->subname(i, OriginNames[i]);
    }
    for (auto *stat : {&unknownChecksByPcOrigin, &unknownHoldsByPcOrigin,
                       &unknownStlfByPcOrigin, &unknownProducerByPc}) {
        stat->init(MaxPcEntries + 1);
        stat->flags(statistics::total | statistics::nozero);
    }
    resetPcLabels();
    scaledInputStates.init(16).flags(statistics::total | statistics::nozero);
    scaledResults.init(4).flags(statistics::total | statistics::nozero);
    scaledResults.subname(0, "known");
    scaledResults.subname(1, "none");
    scaledResults.subname(2, "inherited_ambiguous");
    scaledResults.subname(3, "fresh_ambiguous");
    scaledMagnitude.init(3).flags(statistics::total | statistics::nozero);
    scaledUnknownMagnitude.init(3).flags(statistics::total | statistics::nozero);
    scaledUnknownMagnitudeByState.init(48).flags(
        statistics::total | statistics::nozero);
    for (unsigned m = 0; m < 3; ++m) {
        scaledMagnitude.subname(m, MagnitudeNames[m]);
        scaledUnknownMagnitude.subname(m, MagnitudeNames[m]);
    }
    for (unsigned index = 0; index < 4; ++index) {
        for (unsigned base = 0; base < 4; ++base) {
            const unsigned pair = index * 4 + base;
            const std::string label = std::string("index_") + StateNames[index]
                + "_base_" + StateNames[base];
            scaledInputStates.subname(pair, label);
            for (unsigned m = 0; m < 3; ++m) {
                scaledUnknownMagnitudeByState.subname(pair * 3 + m,
                    label + "_" + MagnitudeNames[m]);
            }
        }
    }
}

void
KlpProvStats::resetPcLabels()
{
    for (auto *stat : {&unknownChecksByPcOrigin, &unknownHoldsByPcOrigin,
                       &unknownStlfByPcOrigin, &unknownProducerByPc}) {
        for (unsigned i = 0; i < MaxPcEntries; ++i)
            stat->subname(i, "unused_" + std::to_string(i));
        stat->subname(MaxPcEntries, "overflow");
    }
}

void
KlpProvStats::resetStats()
{
    statistics::Group::resetStats();
    loadKeys.clear();
    producerKeys.clear();
    resetPcLabels();
    // Register origin fields intentionally survive stats reset/warmup.
}

unsigned
KlpProvStats::loadSlot(Addr pc, const AddrProv &prov)
{
    const LoadKey key{pc, prov.origin, prov.originPc};
    const auto it = loadKeys.find(key);
    if (it != loadKeys.end())
        return it->second;
    if (loadKeys.size() == MaxPcEntries)
        return MaxPcEntries;
    const unsigned slot = loadKeys.size();
    loadKeys.emplace(key, slot);
    // PCs can be near 0x5000000000000000. SparseHistogram's double key
    // would merge distinct instructions; preserve all bits in string labels.
    std::ostringstream label;
    label << "load_0x" << std::hex << pc << "_"
          << OriginNames[originIndex(prov.origin)] << "_origin_0x"
          << prov.originPc;
    for (auto *stat : {&unknownChecksByPcOrigin, &unknownHoldsByPcOrigin,
                       &unknownStlfByPcOrigin})
        stat->subname(slot, label.str());
    return slot;
}

unsigned
KlpProvStats::producerSlot(Addr pc, const char *mnemonic,
                           AddrProvOrigin reason, bool fresh)
{
    const ProducerKey key{pc, mnemonic, reason, fresh};
    const auto it = producerKeys.find(key);
    if (it != producerKeys.end())
        return it->second;
    if (producerKeys.size() == MaxPcEntries)
        return MaxPcEntries;
    const unsigned slot = producerKeys.size();
    producerKeys.emplace(key, slot);
    std::ostringstream label;
    label << "pc_0x" << std::hex << pc << "_" << mnemonic << "_"
          << (fresh ? "fresh_" : "propagated_")
          << OriginNames[originIndex(reason)];
    unknownProducerByPc.subname(slot, label.str());
    return slot;
}

void
KlpProvStats::recordLoad(Addr pc, const AddrProv &prov, LoadPhase phase)
{
    statistics::Vector *states = nullptr;
    statistics::Vector *origins = nullptr;
    statistics::Vector *pcs = nullptr;
    switch (phase) {
      case LoadPhase::Check:
        states = &checkStates;
        origins = &checkOrigins;
        pcs = &unknownChecksByPcOrigin;
        break;
      case LoadPhase::Hold:
        states = &holdStates;
        origins = &holdOrigins;
        pcs = &unknownHoldsByPcOrigin;
        break;
      case LoadPhase::StlfBlocked:
        states = &stlfBlockedStates;
        origins = &stlfBlockedOrigins;
        pcs = &unknownStlfByPcOrigin;
        break;
    }
    assert(states && origins && pcs);
    ++(*states)[static_cast<unsigned>(prov.state)];
    ++(*origins)[originIndex(prov.origin)];
    if (unavailable(prov))
        ++(*pcs)[loadSlot(pc, prov)];
}

void
KlpProvStats::recordProducer(Addr pc, const char *mnemonic,
                             const AddrProv &prov, bool fresh,
                             AddrProvOrigin step_reason)
{
    if (fresh)
        ++freshOrigins[originIndex(prov.origin)];
    else
        ++propagatedOrigins[originIndex(prov.origin)];
    if (unavailable(prov))
        ++unknownProducerByPc[producerSlot(pc, mnemonic, step_reason, fresh)];
}

void
KlpProvStats::recordScaled(const AddrProv &index, const AddrProv &base,
                           const AddrProv &result, RegVal index_value,
                           RegVal base_value, unsigned xlen)
{
    const unsigned pair = static_cast<unsigned>(index.state) * 4 +
                          static_cast<unsigned>(base.state);
    ++scaledInputStates[pair];
    const RegVal index_mag = addrProvMagnitude(index_value, xlen);
    const RegVal base_mag = addrProvMagnitude(base_value, xlen);
    const unsigned outcome = base_mag > index_mag ? 0 :
                             index_mag > base_mag ? 1 : 2;
    ++scaledMagnitude[outcome];
    if (!unavailable(result)) {
        ++scaledResults[0];
        return;
    }
    if (result.state == AddrProv::State::NONE)
        ++scaledResults[1];
    else
        // Incoming ambiguity does not survive the new comparison: even a
        // rejected result has a fresh cause and producer PC.
        ++scaledResults[3];
    ++scaledUnknownMagnitude[outcome];
    ++scaledUnknownMagnitudeByState[pair * 3 + outcome];
}

} // namespace o3
} // namespace gem5
