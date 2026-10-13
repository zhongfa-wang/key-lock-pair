/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __MEM_CACHE_GRANULE_COVERAGE_HH__
#define __MEM_CACHE_GRANULE_COVERAGE_HH__

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <unordered_map>
#include <utility>

namespace gem5
{

/**
 * Exact, access-weighted dominant-PC/key coverage for aligned granules.
 * The caller splits requests at the configured granule boundaries.
 *
 * Only live cache residencies retain frequency tables. Incrementing a global
 * numerator whenever a residency's maximum rises accounts for both retired
 * and live residencies, so reading totals never needs to finalize residents.
 * A statistics reset starts a new observation epoch, including for residents
 * that remain physically cached across the reset.
 */
class GranuleCoverageTracker
{
  public:
    struct Totals
    {
        uint64_t accesses = 0;
        uint64_t pcCoveredAccesses = 0;
        uint64_t pcValidKeyCoveredAccesses = 0;
        uint64_t keyCoveredAccesses = 0;
        uint64_t unknownPcAccesses = 0;
        uint64_t unknownKeyAccesses = 0;
        uint64_t lifetimes = 0;
    };

  private:
    using Counts = std::unordered_map<uint64_t, uint64_t>;

    struct Residency
    {
        Counts pcCounts;
        Counts pcValidKeyCounts;
        Counts keyCounts;
        uint64_t pcMax = 0;
        uint64_t pcValidKeyMax = 0;
        uint64_t keyMax = 0;
    };

    // Address identity survives movement between cache block objects.
    std::map<std::pair<uint64_t, bool>, Residency> residents;
    Totals accumulated;

    static void
    recordValue(Counts &counts, uint64_t value, uint64_t &maximum,
                uint64_t &covered)
    {
        const uint64_t count = ++counts[value];
        if (count > maximum) {
            covered += count - maximum;
            maximum = count;
        }
    }

  public:
    /** Record one access to an aligned granule in its current residency. */
    void
    record(uint64_t granule_addr, bool secure, std::optional<uint64_t> pc,
           std::optional<uint64_t> key)
    {
        auto [it, inserted] = residents.try_emplace(
            std::make_pair(granule_addr, secure));
        if (inserted)
            ++accumulated.lifetimes;

        ++accumulated.accesses;
        auto &resident = it->second;
        if (pc) {
            recordValue(resident.pcCounts, *pc, resident.pcMax,
                        accumulated.pcCoveredAccesses);
        } else {
            ++accumulated.unknownPcAccesses;
        }
        if (key) {
            // Select the dominant PC independently on valid-key accesses.
            // Unknown keys still contribute to the shared access denominator.
            if (pc) {
                recordValue(resident.pcValidKeyCounts, *pc,
                            resident.pcValidKeyMax,
                            accumulated.pcValidKeyCoveredAccesses);
            }
            recordValue(resident.keyCounts, *key, resident.keyMax,
                        accumulated.keyCoveredAccesses);
        } else {
            ++accumulated.unknownKeyAccesses;
        }
    }

    /** End every observed granule residency in this line/security domain. */
    void
    eraseLine(uint64_t line_addr, unsigned line_bytes, bool secure)
    {
        auto it = residents.lower_bound(std::make_pair(line_addr, false));
        // Subtraction also handles a final line ending at UINT64_MAX.
        while (it != residents.end() &&
               it->first.first - line_addr < line_bytes) {
            if (it->first.second == secure)
                it = residents.erase(it);
            else
                ++it;
        }
    }

    void
    reset()
    {
        residents.clear();
        accumulated = {};
    }

    const Totals &totals() const { return accumulated; }
    std::size_t liveGranules() const { return residents.size(); }
};

} // namespace gem5

#endif // __MEM_CACHE_GRANULE_COVERAGE_HH__
