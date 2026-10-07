// [klp] {
#ifndef __CPU_ADDR_PROV_HH__
#define __CPU_ADDR_PROV_HH__

#include <cassert>
#include <cstdint>

#include "base/types.hh"

namespace gem5
{

// ISA decoders annotate special cases; Default retains ordinary two-source
// arithmetic and one-source propagation for otherwise eligible instructions.
enum class AddrProvRule : uint8_t {
    Default,
    SeedConstantOrPreserve,
    PreserveFirst,
    PreserveSelected,
    SeedStrongResult,
    SeedWeakResult,
    ScaledIndexBase,
    ScaledIndexBasePreserve
};

// ISA semantic operation annotations for candidate propagation.
// They are independent of diagnostic ancestry.
enum class AddrProvValueOp : uint8_t {
    Other, Auipc, AddImmediate, PlainAdd, Copy, Or, Xor
};

// Pure diagnostic ancestry. Neither state transitions nor key generation may
// inspect these fields. A propagated NONE/AMBIGUOUS retains its first origin.
enum class AddrProvOrigin : uint8_t {
    Untracked,
    ArchitecturalReset,
    CpuTakeover,
    Unsupported,
    FaultOrPredicate,
    MemoryPending,
    ZeroRegister,
    MagnitudeTie,
    LogicalTie,
    ScaledBaseSelected,
    ScaledIndexDominant,
    ScaledMagnitudeTie,
    ArithmeticSelected,
    StrongSeed,
    WeakSeed,
    ConstantSeed,
    LoadSeed,
    BothNegative,
    CompareUnavailable,
    ThresholdBothAbove,
    ThresholdNeitherAbove,
    NumOrigins
};

struct AddrProv
{
    enum class State : uint8_t {
        NONE,
        WEAK,
        STRONG,
        AMBIGUOUS
    };

    State state = State::NONE;
    RegVal candidate = 0;
    AddrProvOrigin origin = AddrProvOrigin::Untracked;
    Addr originPc = 0;
    // Semantic marker for an AUIPC address awaiting its completing operation.
    // Only a value-preserving copy may propagate this marker unchanged.
    bool auipcPending = false;
    // Diagnostic only: this ambiguity was introduced by the threshold policy,
    // while the old policy on the same inputs would not have been AMBIGUOUS.
    bool thresholdIntroducedAmbiguity = false;
};

/*
 * Canonical AddrProv constructors.
 *
 * NONE clears its candidate. AMBIGUOUS retains diagnostic provenance,
 * but its candidate must never authorize a speculative load.
 */
[[nodiscard]] inline constexpr AddrProv
noneAddrProv(AddrProvOrigin origin = AddrProvOrigin::Untracked,
             Addr origin_pc = 0)
{
    return {
        AddrProv::State::NONE,
        0,
        origin,
        origin_pc
    };
}

[[nodiscard]] inline constexpr AddrProv
weakAddrProv(RegVal candidate,
             AddrProvOrigin origin = AddrProvOrigin::Untracked,
             Addr origin_pc = 0)
{
    return {
        AddrProv::State::WEAK,
        candidate,
        origin,
        origin_pc
    };
}

[[nodiscard]] inline constexpr AddrProv
strongAddrProv(RegVal candidate,
             AddrProvOrigin origin = AddrProvOrigin::Untracked,
             Addr origin_pc = 0)
{
    return {
        AddrProv::State::STRONG,
        candidate,
        origin,
        origin_pc
    };
}

[[nodiscard]] inline constexpr AddrProv
ambiguousAddrProv(RegVal candidate,
             AddrProvOrigin origin = AddrProvOrigin::Untracked,
             Addr origin_pc = 0)
{
    return {
        AddrProv::State::AMBIGUOUS,
        candidate,
        origin,
        origin_pc
    };
}

/*
 * Logic and shifts carry the selected provenance unchanged, even when they
 * change the architectural value. They never create a candidate or resolve
 * ambiguity.
 */
[[nodiscard]] inline constexpr AddrProv
propagateBasePreserving(const AddrProv &src)
{
    switch (src.state) {
      case AddrProv::State::NONE:
        return noneAddrProv(src.origin, src.originPc);
      case AddrProv::State::WEAK:
        return weakAddrProv(src.candidate, src.origin, src.originPc);
      case AddrProv::State::STRONG:
        return strongAddrProv(src.candidate, src.origin, src.originPc);
      case AddrProv::State::AMBIGUOUS: {
        auto result = ambiguousAddrProv(src.candidate, src.origin, src.originPc);
        result.thresholdIntroducedAmbiguity = src.thresholdIntroducedAmbiguity;
        return result;
      }
    }
    return ambiguousAddrProv(0);
}

// The comparator selects an operand; it does not select a candidate policy.
// A single comparison result is shared by propagation and diagnostics.
enum class AddrProvCompareSelection : uint8_t {
    Left, Right, Undecided
};

enum class AddrProvCompareReason : uint8_t {
    NonnegativeLeft,
    NonnegativeRight,
    LeftNegative,
    RightNegative,
    BothNegative,
    Equal,
    UnsupportedXlen,
    MissingOperand,
    NoExecutionUnit,
    ThresholdLeft,
    ThresholdRight,
    BothAboveThreshold,
    NeitherAboveThreshold,
    NumReasons
};

struct AddrProvCompare
{
    AddrProvCompareSelection selection = AddrProvCompareSelection::Undecided;
    AddrProvCompareReason reason = AddrProvCompareReason::UnsupportedXlen;
    // Diagnostic payload only: the low-XLEN common nonnegative value on a tie.
    RegVal equalValue = 0;

    [[nodiscard]] constexpr bool decided() const
    {
        return selection != AddrProvCompareSelection::Undecided;
    }
};

/*
 * Old fixed-width comparator, retained ONLY for diagnostic attribution.
 * Its result never controls execution, candidate selection or authorization.
 * Inputs are the already-read/forwarded, original XLEN operands, before any
 * word truncation or SHnADD shift. One negative input selects the nonnegative
 * input; two negative inputs are undecided. Nonnegative inputs use an exact
 * unsigned comparison, with equality also undecided.
 *
 * Each bit supplies (equal, left-greater). The high/low group reduction is a
 * balanced 32- or 64-leaf tree (five or six merge levels), not an absolute-value
 * oracle. The sign leaf is known equal on this path. This combinational model
 * assumes a comparator alongside each participating integer execution lane;
 * it neither performs a register-file read nor models physical timing closure.
 */
[[nodiscard]] inline constexpr AddrProvCompare
compareAddrProvOperandsLegacy(RegVal lhs, RegVal rhs, unsigned xlen)
{
    using Selection = AddrProvCompareSelection;
    using Reason = AddrProvCompareReason;
    if (xlen != 32 && xlen != 64)
        return {Selection::Undecided, Reason::UnsupportedXlen, 0};

    const RegVal mask = xlen == 32 ? 0xffffffffULL : ~RegVal{0};
    lhs &= mask;
    rhs &= mask;
    const bool lhs_negative = (lhs >> (xlen - 1)) & 1;
    const bool rhs_negative = (rhs >> (xlen - 1)) & 1;
    if (lhs_negative && rhs_negative)
        return {Selection::Undecided, Reason::BothNegative, 0};
    if (lhs_negative)
        return {Selection::Right, Reason::LeftNegative, 0};
    if (rhs_negative)
        return {Selection::Left, Reason::RightNegative, 0};

    struct BitGroup
    {
        bool equal = true;
        bool leftGreater = false;
    };
    BitGroup groups[64] = {};
    for (unsigned bit = 0; bit < xlen; ++bit) {
        const bool left = (lhs >> bit) & 1;
        const bool right = (rhs >> bit) & 1;
        groups[bit].equal = !(left ^ right);
        groups[bit].leftGreater = left & !right;
    }
    for (unsigned span = 1; span < xlen; span <<= 1) {
        for (unsigned low = 0; low < xlen; low += span << 1) {
            const BitGroup lower = groups[low];
            const BitGroup upper = groups[low + span];
            groups[low].equal = upper.equal & lower.equal;
            groups[low].leftGreater = upper.leftGreater |
                (upper.equal & lower.leftGreater);
        }
    }
    if (groups[0].equal)
        return {Selection::Undecided, Reason::Equal, lhs};
    if (groups[0].leftGreater)
        return {Selection::Left, Reason::NonnegativeLeft, 0};
    return {Selection::Right, Reason::NonnegativeRight, 0};
}

/*
 * Execution comparator: two original XLEN inputs independently compare with
 * the configured threshold. Sign handling precedes threshold classification.
 * Both-above is ambiguous; neither-above explicitly clears provenance.
 * The two threshold comparisons are combinational sidecars of the allocated
 * integer lane, with the existing issue capacity and completion latency.
 */
[[nodiscard]] inline constexpr AddrProvCompare
compareAddrProvOperands(RegVal lhs, RegVal rhs, unsigned xlen, RegVal threshold)
{
    using Selection = AddrProvCompareSelection;
    using Reason = AddrProvCompareReason;
    if (xlen != 32 && xlen != 64)
        return {Selection::Undecided, Reason::UnsupportedXlen, 0};
    const RegVal mask = xlen == 32 ? 0xffffffffULL : ~RegVal{0};
    lhs &= mask;
    rhs &= mask;
    const bool lhs_negative = (lhs >> (xlen - 1)) & 1;
    const bool rhs_negative = (rhs >> (xlen - 1)) & 1;
    if (lhs_negative && rhs_negative)
        return {Selection::Undecided, Reason::BothNegative, 0};
    if (lhs_negative)
        return {Selection::Right, Reason::LeftNegative, 0};
    if (rhs_negative)
        return {Selection::Left, Reason::RightNegative, 0};
    const bool left_above = lhs > threshold;
    const bool right_above = rhs > threshold;
    if (left_above && right_above)
        return {Selection::Undecided, Reason::BothAboveThreshold, 0};
    if (left_above)
        return {Selection::Left, Reason::ThresholdLeft, 0};
    if (right_above)
        return {Selection::Right, Reason::ThresholdRight, 0};
    return {Selection::Undecided, Reason::NeitherAboveThreshold, 0};
}

// Existing tie-origin names are retained for diagnostic continuity. New
// undecided causes must not be mislabeled as numerical equality.
[[nodiscard]] inline constexpr AddrProv
undecidedAddrProv(const AddrProvCompare &comparison,
                  AddrProvOrigin tie_origin, Addr producer_pc = 0)
{
    assert(!comparison.decided());
    if (comparison.reason == AddrProvCompareReason::NeitherAboveThreshold)
        return noneAddrProv(AddrProvOrigin::ThresholdNeitherAbove, producer_pc);
    if (comparison.reason == AddrProvCompareReason::BothAboveThreshold)
        return ambiguousAddrProv(0, AddrProvOrigin::ThresholdBothAbove,
                                 producer_pc);
    if (comparison.reason == AddrProvCompareReason::Equal) {
        return ambiguousAddrProv(comparison.equalValue, tie_origin,
                                 producer_pc);
    }
    return ambiguousAddrProv(0,
        comparison.reason == AddrProvCompareReason::BothNegative ?
            AddrProvOrigin::BothNegative : AddrProvOrigin::CompareUnavailable,
        producer_pc);
}

/*
 * Default arithmetic creates a fresh candidate from the selected raw input,
 * independently of its incoming provenance. No candidate policy repeats the
 * comparator. Equal nonnegative values retain their common diagnostic value;
 * two-negative and unsupported inputs use a zero AMBIGUOUS payload.
 */
[[nodiscard]] inline constexpr AddrProv
selectAddrProvByComparison(RegVal lhs, RegVal rhs,
                           const AddrProvCompare &comparison,
                           Addr producer_pc = 0)
{
    if (!comparison.decided()) {
        return undecidedAddrProv(comparison, AddrProvOrigin::MagnitudeTie,
                                 producer_pc);
    }
    return strongAddrProv(
        comparison.selection == AddrProvCompareSelection::Left ? lhs : rhs,
        AddrProvOrigin::ArithmeticSelected, producer_pc);
}

// Ordinary ADD inherits the selected source's candidate AND state only when
// available. NONE/AMBIGUOUS sources retain the fresh-candidate fallback.
[[nodiscard]] inline constexpr AddrProv
selectAddrProvPreserving(RegVal lhs, const AddrProv &lhs_prov,
                         RegVal rhs, const AddrProv &rhs_prov,
                         const AddrProvCompare &comparison,
                         Addr producer_pc = 0)
{
    const AddrProv fresh = selectAddrProvByComparison(
        lhs, rhs, comparison, producer_pc);
    if (!comparison.decided())
        return fresh;
    const AddrProv &source =
        comparison.selection == AddrProvCompareSelection::Left ?
            lhs_prov : rhs_prov;
    if (source.state == AddrProv::State::WEAK ||
        source.state == AddrProv::State::STRONG)
        return propagateBasePreserving(source);
    return fresh;
}

// Only callers that prove a value-preserving copy may carry AUIPC pending.
[[nodiscard]] inline constexpr AddrProv
copyAddrProvValue(const AddrProv &source)
{
    return source;
}

[[nodiscard]] inline constexpr AddrProv
completeAuipcAddrProv(const AddrProv &source, RegVal address, Addr pc = 0)
{
    return source.auipcPending ?
        strongAddrProv(address, AddrProvOrigin::StrongSeed, pc) :
        propagateBasePreserving(source);
}

// Register logic copies the selected candidate AND state, including NONE and
// AMBIGUOUS. Successful comparison does not create a credential for logic.
[[nodiscard]] inline constexpr AddrProv
propagateAddrProvByComparison(const AddrProv &lhs_prov,
                             const AddrProv &rhs_prov,
                             const AddrProvCompare &comparison,
                             Addr producer_pc = 0)
{
    if (!comparison.decided()) {
        return undecidedAddrProv(comparison, AddrProvOrigin::LogicalTie,
                                 producer_pc);
    }
    return propagateBasePreserving(
        comparison.selection == AddrProvCompareSelection::Left ?
            lhs_prov : rhs_prov);
}

/*
 * ADD.UW and SH1/2/3ADD (including .uw) use a directional gate: the comparison
 * must select the right operand, rs2. Selecting the index never authorizes a
 * credential, even when it won because only the base was negative. Preserve
 * raw rs2 as the diagnostic payload when the index wins.
 */
[[nodiscard]] inline constexpr AddrProv
selectAddrScaledBaseByComparison(RegVal base_value,
                                 const AddrProvCompare &comparison,
                                 Addr producer_pc = 0)
{
    if (comparison.selection == AddrProvCompareSelection::Right) {
        return strongAddrProv(base_value,
                              AddrProvOrigin::ScaledBaseSelected, producer_pc);
    }
    if (comparison.selection == AddrProvCompareSelection::Left) {
        return ambiguousAddrProv(base_value,
                                 AddrProvOrigin::ScaledIndexDominant, producer_pc);
    }
    return undecidedAddrProv(comparison, AddrProvOrigin::ScaledMagnitudeTie,
                             producer_pc);
}

/*
 * Only after rs2 wins may its existing WEAK/STRONG provenance be inherited,
 * including candidate, state and diagnostic ancestry. NONE/AMBIGUOUS sources
 * use the fresh STRONG raw-rs2 candidate. Known provenance never bypasses an
 * index-selected or undecided comparison.
 */
[[nodiscard]] inline constexpr AddrProv
selectAddrScaledBasePreserving(RegVal base_value, const AddrProv &base_prov,
                               const AddrProvCompare &comparison,
                               Addr producer_pc = 0)
{
    const AddrProv selected = selectAddrScaledBaseByComparison(
        base_value, comparison, producer_pc);
    if (selected.state == AddrProv::State::STRONG &&
        (base_prov.state == AddrProv::State::WEAK ||
         base_prov.state == AddrProv::State::STRONG)) {
        return propagateBasePreserving(base_prov);
    }
    return selected;
}

/*
 * Diagnostic attribution against the OLD policy on this producer's captured
 * inputs. Reuse the actual propagation helpers, including selected-source
 * state and the rs2-only scaled gate; a successful old comparison alone is
 * insufficient. This is not a second execution or a shadow legacy machine.
 */
[[nodiscard]] inline constexpr bool
thresholdIntroducesAmbiguity(AddrProvRule rule, bool preserve_add,
                             RegVal lhs, const AddrProv &lhs_prov,
                             RegVal rhs, const AddrProv &rhs_prov,
                             unsigned xlen, const AddrProvCompare &comparison)
{
    if (comparison.reason != AddrProvCompareReason::BothAboveThreshold)
        return false;
    const auto old = compareAddrProvOperandsLegacy(lhs, rhs, xlen);
    AddrProv old_result;
    switch (rule) {
      case AddrProvRule::Default:
        old_result = preserve_add ?
            selectAddrProvPreserving(lhs, lhs_prov, rhs, rhs_prov, old) :
            selectAddrProvByComparison(lhs, rhs, old);
        break;
      case AddrProvRule::PreserveSelected:
        old_result = propagateAddrProvByComparison(lhs_prov, rhs_prov, old);
        break;
      case AddrProvRule::ScaledIndexBase:
        old_result = selectAddrScaledBaseByComparison(rhs, old);
        break;
      case AddrProvRule::ScaledIndexBasePreserve:
        old_result = selectAddrScaledBasePreserving(rhs, rhs_prov, old);
        break;
      default:
        return false;
    }
    return old_result.state != AddrProv::State::AMBIGUOUS;
}

/*
 * Result of extracting a credential for a load.
 *
 * credential is meaningful only when allowSpeculation is true.
 */
struct CredentialDecision
{
    bool allowSpeculation = false;
    RegVal credential = 0;
};

/*
 * Encapsulated WEAK policy.
 *
 * Changing this function to return false conservatively prevents a load
 * with a standalone WEAK base from executing speculatively.
 *
 * Later this can be replaced by a CPU parameter.
 */
[[nodiscard]] inline constexpr bool
allowWeakCredential()
{
    return true;
}

/*
 * Convert provenance into a speculation/credential decision.
 */
[[nodiscard]] inline constexpr CredentialDecision
decideCredential(const AddrProv &prov)
{
    switch (prov.state) {
      case AddrProv::State::STRONG:
        return {
            true,
            prov.candidate
        };

      case AddrProv::State::WEAK:
        if (allowWeakCredential()) {
            return {
                true,
                prov.candidate
            };
        }

        return {
            false,
            0
        };

      case AddrProv::State::NONE:
      case AddrProv::State::AMBIGUOUS:
        return {
            false,
            0
        };
    }

    // Defensive fallback.
    return {
        false,
        0
    };
}

} // namespace gem5

#endif // __CPU_ADDR_PROV_HH__
// } [klp]