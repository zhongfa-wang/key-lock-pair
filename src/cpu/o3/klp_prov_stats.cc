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
    "load_seed", "both_negative", "compare_unavailable"
};
constexpr std::array<const char *, 4> StateNames = {
    "none", "weak", "strong", "ambiguous"
};
constexpr unsigned NumCompareReasons =
    static_cast<unsigned>(AddrProvCompareReason::NumReasons);
constexpr std::array<const char *, NumCompareReasons> CompareReasonNames = {
    "nonnegative_left", "nonnegative_right", "left_negative", "right_negative",
    "both_negative", "equal", "unsupported_xlen", "missing_operand",
    "no_execution_unit"
};
constexpr std::array<const char *, 3> CompareSelectionNames = {
    "left", "right", "undecided"
};
constexpr std::array<const char *, 5> ScaledComparisonNames = {
    "base_selected", "index_selected", "equal", "both_negative", "unavailable"
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
      ADD_STAT(inputCompareReasons, statistics::units::Count::get(),
               "Operand-comparison reasons, once per comparison request"),
      ADD_STAT(inputCompareSelections, statistics::units::Count::get(),
               "Operand selections, once per comparison request"),
      ADD_STAT(scaledInputStates, statistics::units::Count::get(),
               "Scaled-index-base input states, index then unscaled base"),
      ADD_STAT(scaledResults, statistics::units::Count::get(),
               "Directional ADD.UW/shNadd results; known includes inherited "
               "WEAK/STRONG and fresh STRONG; NONE and inherited_ambiguous "
               "bins remain zero under the current candidate policy"),
      ADD_STAT(scaledComparison, statistics::units::Count::get(),
               "Shared operand-comparison outcomes for ADD.UW/shNadd"),
      ADD_STAT(scaledUnknownComparison, statistics::units::Count::get(),
               "Shared comparison outcomes for unknown ADD.UW/shNadd outputs"),
      ADD_STAT(scaledUnknownComparisonByState, statistics::units::Count::get(),
               "Unknown ADD.UW/shNadd comparison outcomes by input states")
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
    inputCompareReasons.init(CompareReasonNames.size()).flags(
        statistics::total | statistics::nozero);
    inputCompareSelections.init(CompareSelectionNames.size()).flags(
        statistics::total | statistics::nozero);
    for (unsigned i = 0; i < CompareReasonNames.size(); ++i)
        inputCompareReasons.subname(i, CompareReasonNames[i]);
    for (unsigned i = 0; i < CompareSelectionNames.size(); ++i)
        inputCompareSelections.subname(i, CompareSelectionNames[i]);
    scaledInputStates.init(16).flags(statistics::total | statistics::nozero);
    scaledResults.init(4).flags(statistics::total | statistics::nozero);
    scaledResults.subname(0, "known");
    scaledResults.subname(1, "none");
    scaledResults.subname(2, "inherited_ambiguous");
    scaledResults.subname(3, "fresh_ambiguous");
    scaledComparison.init(ScaledComparisonNames.size()).flags(
        statistics::total | statistics::nozero);
    scaledUnknownComparison.init(ScaledComparisonNames.size()).flags(
        statistics::total | statistics::nozero);
    scaledUnknownComparisonByState.init(16 * ScaledComparisonNames.size()).flags(
        statistics::total | statistics::nozero);
    for (unsigned outcome = 0; outcome < ScaledComparisonNames.size(); ++outcome) {
        scaledComparison.subname(outcome, ScaledComparisonNames[outcome]);
        scaledUnknownComparison.subname(outcome, ScaledComparisonNames[outcome]);
    }
    for (unsigned index = 0; index < 4; ++index) {
        for (unsigned base = 0; base < 4; ++base) {
            const unsigned pair = index * 4 + base;
            const std::string label = std::string("index_") + StateNames[index]
                + "_base_" + StateNames[base];
            scaledInputStates.subname(pair, label);
            for (unsigned outcome = 0;
                 outcome < ScaledComparisonNames.size(); ++outcome) {
                scaledUnknownComparisonByState.subname(
                    pair * ScaledComparisonNames.size() + outcome,
                    label + "_" + ScaledComparisonNames[outcome]);
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
KlpProvStats::recordComparison(const AddrProvCompare &comparison)
{
    const unsigned reason = static_cast<unsigned>(comparison.reason);
    const unsigned selection = static_cast<unsigned>(comparison.selection);
    assert(reason < CompareReasonNames.size());
    assert(selection < CompareSelectionNames.size());
    ++inputCompareReasons[reason];
    ++inputCompareSelections[selection];
}

void
KlpProvStats::recordScaled(const AddrProv &index, const AddrProv &base,
                           const AddrProv &result,
                           const AddrProvCompare &comparison)
{
    const unsigned pair = static_cast<unsigned>(index.state) * 4 +
                          static_cast<unsigned>(base.state);
    ++scaledInputStates[pair];
    // Consume the same execution comparison used by candidate propagation;
    // neither recompute operand relations nor count a second request here.
    unsigned outcome = 4;
    if (comparison.selection == AddrProvCompareSelection::Right)
        outcome = 0;
    else if (comparison.selection == AddrProvCompareSelection::Left)
        outcome = 1;
    else if (comparison.reason == AddrProvCompareReason::Equal)
        outcome = 2;
    else if (comparison.reason == AddrProvCompareReason::BothNegative)
        outcome = 3;
    ++scaledComparison[outcome];
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
    ++scaledUnknownComparison[outcome];
    ++scaledUnknownComparisonByState[
        pair * ScaledComparisonNames.size() + outcome];
}

} // namespace o3
} // namespace gem5
