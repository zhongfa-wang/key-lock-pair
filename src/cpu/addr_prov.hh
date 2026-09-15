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
    CompareImmediate,
    PreserveFirst,
    PreserveSelected,
    SeedResult
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
};

/*
 * Canonical AddrProv constructors.
 *
 * NONE and AMBIGUOUS do not carry a usable credential, so their
 * candidate is always cleared to zero.
 */
[[nodiscard]] inline constexpr AddrProv
noneAddrProv()
{
    return {
        AddrProv::State::NONE,
        0
    };
}

[[nodiscard]] inline constexpr AddrProv
weakAddrProv(RegVal candidate)
{
    return {
        AddrProv::State::WEAK,
        candidate
    };
}

[[nodiscard]] inline constexpr AddrProv
strongAddrProv(RegVal candidate)
{
    return {
        AddrProv::State::STRONG,
        candidate
    };
}

[[nodiscard]] inline constexpr AddrProv
ambiguousAddrProv()
{
    return {
        AddrProv::State::AMBIGUOUS,
        0
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
        return noneAddrProv();
      case AddrProv::State::WEAK:
        return weakAddrProv(src.candidate);
      case AddrProv::State::STRONG:
        return strongAddrProv(src.candidate);
      case AddrProv::State::AMBIGUOUS:
        return ambiguousAddrProv();
    }
    return ambiguousAddrProv();
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
 * Preserve the raw register/immediate value, not its magnitude or the result.
 */
[[nodiscard]] inline constexpr AddrProv
selectAddrProvByMagnitude(RegVal lhs, RegVal rhs, unsigned xlen)
{
    const RegVal lhs_magnitude = addrProvMagnitude(lhs, xlen);
    const RegVal rhs_magnitude = addrProvMagnitude(rhs, xlen);
    if (lhs_magnitude == rhs_magnitude)
        return ambiguousAddrProv();
    return strongAddrProv(lhs_magnitude > rhs_magnitude ? lhs : rhs);
}

// Register logic selects a source by current operand values, then copies
// that source's candidate AND state, including NONE and AMBIGUOUS.
[[nodiscard]] inline constexpr AddrProv
propagateAddrProvByMagnitude(RegVal lhs, const AddrProv &lhs_prov,
                            RegVal rhs, const AddrProv &rhs_prov,
                            unsigned xlen)
{
    const RegVal lhs_magnitude = addrProvMagnitude(lhs, xlen);
    const RegVal rhs_magnitude = addrProvMagnitude(rhs, xlen);
    if (lhs_magnitude == rhs_magnitude)
        return ambiguousAddrProv();
    return propagateBasePreserving(
        lhs_magnitude > rhs_magnitude ? lhs_prov : rhs_prov);
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