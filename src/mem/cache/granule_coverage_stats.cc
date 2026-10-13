/*
 * Copyright (c) 2026
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#include "mem/cache/granule_coverage_stats.hh"

#include <algorithm>
#include <cassert>
#include <optional>

#include "base/trace.hh"
#include "debug/GranuleCoverage.hh"
#include "mem/packet.hh"

namespace gem5
{

GranuleCoverageStats::GranuleCoverageStats(
    statistics::Group *parent, const std::string &cache_name, uint64_t key_mask,
    unsigned granule_bytes, bool loads_only)
    : statistics::Group(parent,
                        loads_only ? "granuleLoadCoverage" : "granuleCoverage"),
      debugName(cache_name +
                (loads_only ? ".granuleLoadCoverage" : ".granuleCoverage")),
      keyMask(key_mask), granuleSize(granule_bytes), loadsOnly(loads_only),
      ADD_STAT(granuleBytes, statistics::units::Byte::get(),
               "Coverage granule size in bytes; matches tag_granularity"),
      ADD_STAT(accesses, statistics::units::Count::get(),
               loads_only ?
               "Serviced demand loads to resident L1D granules; "
               "one per touched granule, including unknown PC/key; "
               "stores and read-write requests excluded" :
               "Serviced demand accesses to resident L1D granules; "
               "one per touched granule, including unknown PC/key"),
      ADD_STAT(pcCoveredAccesses, statistics::units::Count::get(),
               "Sum of per-residency maximum PC frequencies, including "
               "currently resident granules"),
      ADD_STAT(pcValidKeyCoveredAccesses, statistics::units::Count::get(),
               "Sum of per-residency maximum PC frequencies restricted to "
               "valid-key accesses, including currently resident granules"),
      ADD_STAT(keyCoveredAccesses, statistics::units::Count::get(),
               "Sum of per-residency maximum valid key frequencies, "
               "including currently resident granules"),
      ADD_STAT(unknownPcAccesses, statistics::units::Count::get(),
               "Granule accesses with no request PC; uncovered by any PC"),
      ADD_STAT(unknownKeyAccesses, statistics::units::Count::get(),
               "Granule accesses without a valid KLP key; uncovered by any key"),
      ADD_STAT(lifetimes, statistics::units::Count::get(),
               loads_only ?
               "Granule residency intervals touched by loads in this statistics "
               "epoch, including live and ROI-truncated intervals" :
               "Granule residency intervals touched in this statistics epoch, "
               "including live and ROI-truncated intervals"),
      ADD_STAT(pcCoverage, statistics::units::Ratio::get(),
               "pcCoveredAccesses / accesses; zero if no accesses"),
      ADD_STAT(pcValidKeyCoverage, statistics::units::Ratio::get(),
               "pcValidKeyCoveredAccesses / accesses; unknown keys stay in "
               "denominator; zero if no accesses"),
      ADD_STAT(keyCoverage, statistics::units::Ratio::get(),
               "keyCoveredAccesses / accesses; unknown keys stay in denominator; "
               "zero if no accesses")
{
    assert(granuleSize > 0);
    pcCoverage.precision(9);
    pcValidKeyCoverage.precision(9);
    keyCoverage.precision(9);
}

void
GranuleCoverageStats::record(Packet *pkt)
{
    // The caller supplies a valid real L1D block. Count serviced data requests,
    // not probes, prefetches, writebacks, or permission-only installations.
    if ((!pkt->isRead() && !pkt->isWrite()) || pkt->fromCache() ||
        pkt->cmd.isPrefetch() || pkt->isEviction() ||
        pkt->isKlpLockInstall() || pkt->req->isUncacheable() ||
        pkt->req->isCacheMaintenance() || pkt->req->isInstFetch()) {
        return;
    }

    // A read-write request (e.g. an AMO) must not enter load-only counts.
    // Stores still affect the real cache residency and lock state normally.
    if (loadsOnly && (!pkt->isRead() || pkt->isWrite()))
        return;

    const std::optional<uint64_t> pc = pkt->req->hasPC() ?
        std::optional<uint64_t>(pkt->req->getPC()) : std::nullopt;
    std::optional<uint64_t> key;
    // Unknown and ordinary non-KLP packets must not be grouped under key 0:
    // zero is also a legitimate hash value when the validity marker is set.
    if ((pkt->isKlpRead || pkt->isKlpWrite) && !pkt->isBaseUnknown() &&
        (pkt->getSecTag() & (uint64_t{1} << 63))) {
        key = pkt->getSecTag() & keyMask;
    }

    const auto &enabled = pkt->req->getByteEnable();
    const unsigned size = pkt->getSize();
    assert(enabled.empty() || enabled.size() == size);
    // Raw requests let a trace consumer verify segmentation independently.
    if (GEM5_UNLIKELY(TRACING_ON && debug::GranuleCoverage)) {
        std::string byte_enable(size, '1');
        for (unsigned i = 0; i < enabled.size(); ++i)
            byte_enable[i] = enabled[i] ? '1' : '0';
        DPRINTF(GranuleCoverage,
                "request addr=%#x size=%u secure=%d pc_valid=%d pc=%#x "
                "key_valid=%d key=%#x read=%d write=%d granule_bytes=%u "
                "enabled=%s\n", pkt->getAddr(), size, pkt->isSecure(),
                pc.has_value(), pc.value_or(0), key.has_value(),
                key.value_or(0), pkt->isRead(), pkt->isWrite(),
                granuleSize, byte_enable);
    }
    for (unsigned offset = 0; offset < size;) {
        const uint64_t addr = pkt->getAddr() + offset;
        const unsigned in_granule = addr % granuleSize;
        const uint64_t granule = addr - in_granule;
        const unsigned end = offset +
            std::min(size - offset, granuleSize - in_granule);
        // A masked request accesses a granule only if it enables a byte in it.
        if (enabled.empty() || std::any_of(enabled.begin() + offset,
                enabled.begin() + end, [](bool bit) { return bit; })) {
            tracker.record(granule, pkt->isSecure(), pc, key);
            DPRINTF(GranuleCoverage,
                    "access granule=%#x secure=%d pc_valid=%d pc=%#x "
                    "key_valid=%d key=%#x read=%d write=%d\n",
                    granule, pkt->isSecure(), pc.has_value(), pc.value_or(0),
                    key.has_value(), key.value_or(0),
                    pkt->isRead(), pkt->isWrite());
        }
        offset = end;
    }
}

void
GranuleCoverageStats::endLine(uint64_t addr, unsigned bytes, bool secure)
{
    tracker.eraseLine(addr, bytes, secure);
    DPRINTF(GranuleCoverage, "end line=%#x bytes=%u secure=%d\n",
            addr, bytes, secure);
}

void
GranuleCoverageStats::resetStats()
{
    statistics::Group::resetStats();
    tracker.reset();
    DPRINTF(GranuleCoverage, "reset\n");
}

void
GranuleCoverageStats::preDumpStats()
{
    statistics::Group::preDumpStats();
    // The tracker updates the global sum only when a live maximum increases.
    // Dumps therefore include live residencies without ending or double-
    // counting them, even if the most frequent PC/key changes after a dump.
    const auto &t = tracker.totals();
    granuleBytes = granuleSize;
    accesses = t.accesses;
    pcCoveredAccesses = t.pcCoveredAccesses;
    pcValidKeyCoveredAccesses = t.pcValidKeyCoveredAccesses;
    keyCoveredAccesses = t.keyCoveredAccesses;
    unknownPcAccesses = t.unknownPcAccesses;
    unknownKeyAccesses = t.unknownKeyAccesses;
    lifetimes = t.lifetimes;
    pcCoverage = t.accesses ? double(t.pcCoveredAccesses) / t.accesses : 0.0;
    pcValidKeyCoverage = t.accesses ?
        double(t.pcValidKeyCoveredAccesses) / t.accesses : 0.0;
    keyCoverage = t.accesses ? double(t.keyCoveredAccesses) / t.accesses : 0.0;
    DPRINTF(GranuleCoverage, "dump\n");
}

} // namespace gem5
