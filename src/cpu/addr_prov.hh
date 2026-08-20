// [klp] {
#ifndef __CPU_ADDR_PROV_HH__
#define __CPU_ADDR_PROV_HH__

#include <cstdint>

#include "base/types.hh"

namespace gem5
{

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
 * Propagate provenance through an explicitly base-preserving operation,
 * for example:
 *
 *     addi rd, rs1, imm
 *     c.addi
 *     c.mv
 *
 * STRONG and WEAK preserve their state.
 *
 * AMBIGUOUS remains AMBIGUOUS.
 * NONE remains NONE.
 */
[[nodiscard]] inline constexpr AddrProv
propagateBasePreserving(const AddrProv &src, RegVal dest_value)
{
    switch (src.state) {
      case AddrProv::State::NONE:
        return noneAddrProv();

      case AddrProv::State::WEAK:
        return weakAddrProv(dest_value);

      case AddrProv::State::STRONG:
        return strongAddrProv(dest_value);

      case AddrProv::State::AMBIGUOUS:
        return ambiguousAddrProv();
    }

    // Defensive fallback for a corrupted/invalid enum value.
    return ambiguousAddrProv();
}

/*
 * Merge the provenance of:
 *
 *     add rd, rs1, rs2
 *
 * dest_value must be the actual runtime value written to rd.
 */
[[nodiscard]] inline constexpr AddrProv
mergeAddrAdd(
    const AddrProv &lhs,
    const AddrProv &rhs,
    RegVal dest_value)
{
    using State = AddrProv::State;

    /*
     * Ambiguity is sticky through the dependency chain.
     */
    if (lhs.state == State::AMBIGUOUS ||
        rhs.state == State::AMBIGUOUS) {
        return ambiguousAddrProv();
    }

    /*
     * NONE + NONE -> NONE
     */
    if (lhs.state == State::NONE &&
        rhs.state == State::NONE) {
        return noneAddrProv();
    }

    /*
     * NONE + STRONG/WEAK
     *
     * Only one operand has address provenance. The ADD result remains
     * based on that operand, and the new candidate is the actual rd value.
     */
    if (lhs.state == State::NONE) {
        if (rhs.state == State::STRONG)
            return strongAddrProv(dest_value);

        if (rhs.state == State::WEAK)
            return weakAddrProv(dest_value);

        return noneAddrProv();
    }

    /*
     * STRONG/WEAK + NONE
     */
    if (rhs.state == State::NONE) {
        if (lhs.state == State::STRONG)
            return strongAddrProv(dest_value);

        if (lhs.state == State::WEAK)
            return weakAddrProv(dest_value);

        return noneAddrProv();
    }

    /*
     * STRONG + WEAK
     *
     * The weak operand is treated as a dynamic address component.
     * Preserve the candidate of the strong source instead of using
     * the full ADD result.
     */
    if (lhs.state == State::STRONG &&
        rhs.state == State::WEAK) {
        return strongAddrProv(lhs.candidate);
    }

    /*
     * WEAK + STRONG
     */
    if (lhs.state == State::WEAK &&
        rhs.state == State::STRONG) {
        return strongAddrProv(rhs.candidate);
    }

    /*
     * Remaining combinations:
     *
     *     WEAK   + WEAK
     *     STRONG + STRONG
     *
     * There are two candidates and this version does not perform
     * branch-based or value-based disambiguation.
     */
    return ambiguousAddrProv();
}

// [KLPFIXME] doesn't support sub for now

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