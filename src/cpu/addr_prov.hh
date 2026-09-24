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
      case AddrProv::State::AMBIGUOUS:
        return ambiguousAddrProv(src.candidate, src.origin, src.originPc);
    }
    return ambiguousAddrProv(0);
}

/*
 * Interpret the low XLEN bits as a signed integer, but compute its magnitude
 * using unsigned arithmetic so that abs(INT_MIN) is representable as well.
 * Word operations still use the full XLEN, not their 32-bit arithmetic width.
 */
[[nodiscard]] inline constexpr RegVal
addrProvMagnitude(RegVal value, unsigned xlen)
{
    assert(xlen == 32 || xlen == 64);
    const RegVal mask = xlen == 32 ? 0xffffffffULL : ~RegVal{0};
    const RegVal sign = RegVal{1} << (xlen - 1);
    value &= mask;
    return (value & sign) ? (RegVal{0} - value) & mask : value;
}

/*
 * Each arithmetic operation selects a fresh candidate from its actual
 * inputs. Incoming provenance is deliberately irrelevant: the most recent
 * operation replaces even an AMBIGUOUS candidate. Equal magnitudes cannot
 * distinguish a base from an offset and therefore prohibit key generation.
 * Preserve the winning raw register value. Ties retain their common magnitude.
 */
[[nodiscard]] inline constexpr AddrProv
selectAddrProvByMagnitude(RegVal lhs, RegVal rhs, unsigned xlen,
                          Addr producer_pc = 0)
{
    const RegVal lhs_magnitude = addrProvMagnitude(lhs, xlen);
    const RegVal rhs_magnitude = addrProvMagnitude(rhs, xlen);
    if (lhs_magnitude == rhs_magnitude)
        return ambiguousAddrProv(lhs_magnitude,
                                 AddrProvOrigin::MagnitudeTie, producer_pc);
    return strongAddrProv(lhs_magnitude > rhs_magnitude ? lhs : rhs,
                          AddrProvOrigin::ArithmeticSelected, producer_pc);
}

// Register logic selects a source by current operand values, then copies
// that source's candidate AND state, including NONE and AMBIGUOUS.
[[nodiscard]] inline constexpr AddrProv
propagateAddrProvByMagnitude(RegVal lhs, const AddrProv &lhs_prov,
                            RegVal rhs, const AddrProv &rhs_prov,
                            unsigned xlen, Addr producer_pc = 0)
{
    const RegVal lhs_magnitude = addrProvMagnitude(lhs, xlen);
    const RegVal rhs_magnitude = addrProvMagnitude(rhs, xlen);
    if (lhs_magnitude == rhs_magnitude)
        return ambiguousAddrProv(lhs_magnitude,
                                 AddrProvOrigin::LogicalTie, producer_pc);
    return propagateBasePreserving(
        lhs_magnitude > rhs_magnitude ? lhs_prov : rhs_prov);
}

/*
 * Fresh-candidate policy for ADD.UW, also used as the numeric gate for
 * SH1/2/3ADD and their .uw forms. Compare the original full-XLEN operands,
 * before any ISA shift or .uw truncation. Only a strictly larger base
 * magnitude creates a credential, regardless of incoming provenance.
 *
 * Preserve raw rs2 on success AND when the index is larger. In the latter
 * case the result is AMBIGUOUS and cannot authorize speculation. Equal
 * magnitudes retain their common absolute value, as ordinary R-type ties do.
 */
[[nodiscard]] inline constexpr AddrProv
selectAddrScaledBaseByMagnitude(RegVal index_value, RegVal base_value,
                                unsigned xlen, Addr producer_pc = 0)
{
    const RegVal index_magnitude = addrProvMagnitude(index_value, xlen);
    const RegVal base_magnitude = addrProvMagnitude(base_value, xlen);
    if (base_magnitude > index_magnitude)
        return strongAddrProv(base_value,
                              AddrProvOrigin::ScaledBaseSelected, producer_pc);
    if (index_magnitude > base_magnitude)
        return ambiguousAddrProv(base_value,
                                 AddrProvOrigin::ScaledIndexDominant, producer_pc);
    return ambiguousAddrProv(base_magnitude,
                             AddrProvOrigin::ScaledMagnitudeTie, producer_pc);
}

/*
 * SH1/2/3ADD and their .uw forms retain the same directional numeric gate.
 * Only after rs2 wins may its existing WEAK/STRONG provenance be inherited,
 * including its state and diagnostic ancestry. NONE/AMBIGUOUS sources use
 * the fresh STRONG raw-rs2 candidate selected by the numeric gate instead.
 * A known source never overrides an index-dominant or tied rejection.
 */
[[nodiscard]] inline constexpr AddrProv
selectAddrScaledBasePreserving(RegVal index_value, RegVal base_value,
                               const AddrProv &base_prov, unsigned xlen,
                               Addr producer_pc = 0)
{
    const AddrProv selected = selectAddrScaledBaseByMagnitude(
        index_value, base_value, xlen, producer_pc);
    if (selected.state == AddrProv::State::STRONG &&
        (base_prov.state == AddrProv::State::WEAK ||
         base_prov.state == AddrProv::State::STRONG)) {
        return propagateBasePreserving(base_prov);
    }
    return selected;
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