/*
 * Copyright (c) 2026
 * All rights reserved.
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __MEM_CACHE_GRANULE_COVERAGE_STATS_HH__
#define __MEM_CACHE_GRANULE_COVERAGE_STATS_HH__

#include <string>

#include "base/statistics.hh"
#include "mem/cache/granule_coverage.hh"

namespace gem5
{

class Packet;

/** Observational statistics; no simulated latency or cache-state changes. */
class GranuleCoverageStats : public statistics::Group
{
  public:
    GranuleCoverageStats(statistics::Group *parent,
                         const std::string &cache_name, uint64_t key_mask,
                         unsigned granule_bytes, bool loads_only = false);
    void record(Packet *pkt);
    void endLine(uint64_t addr, unsigned bytes, bool secure);
    void resetStats() override;
    void preDumpStats() override;
    const std::string &name() const { return debugName; }

  private:
    const std::string debugName;
    const uint64_t keyMask;
    const unsigned granuleSize;
    const bool loadsOnly;
    GranuleCoverageTracker tracker;
    statistics::Scalar granuleBytes;
    statistics::Scalar accesses;
    statistics::Scalar pcCoveredAccesses;
    statistics::Scalar pcValidKeyCoveredAccesses;
    statistics::Scalar keyCoveredAccesses;
    statistics::Scalar unknownPcAccesses;
    statistics::Scalar unknownKeyAccesses;
    statistics::Scalar lifetimes;
    statistics::Scalar pcCoverage;
    statistics::Scalar pcValidKeyCoverage;
    statistics::Scalar keyCoverage;
};

} // namespace gem5
#endif // __MEM_CACHE_GRANULE_COVERAGE_STATS_HH__
