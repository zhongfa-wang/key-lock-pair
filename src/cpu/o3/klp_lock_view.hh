/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __CPU_O3_KLP_LOCK_VIEW_HH__
#define __CPU_O3_KLP_LOCK_VIEW_HH__

#include <algorithm>
#include <cstdint>
#include <vector>

namespace gem5::o3
{

/** Per-execution-attempt lock forwarding. Cache snapshots never mutate it. */
class KlpLockView
{
  private:
    struct Update
    {
        uint64_t granule;
        bool secure;
        uint64_t seq;
        uint64_t key;
    };
    std::vector<Update> updates;

  public:
    /** Return whether this is a new, ordered update (including mismatches). */
    bool
    update(uint64_t granule, bool secure, uint64_t seq, uint64_t key)
    {
        for (auto &old : updates) {
            if (old.granule == granule && old.secure == secure) {
                if (seq <= old.seq)
                    return false;
                old = {granule, secure, seq, key};
                return true;
            }
        }
        updates.push_back({granule, secure, seq, key});
        return true;
    }

    bool
    lookup(uint64_t granule, bool secure, uint64_t &key) const
    {
        for (const auto &update : updates) {
            if (update.granule == granule && update.secure == secure) {
                key = update.key;
                return true;
            }
        }
        return false;
    }

    void
    invalidate(uint64_t line, uint64_t lineMask, bool secure)
    {
        updates.erase(std::remove_if(updates.begin(), updates.end(),
            [=](const Update &update) {
                return (update.granule & lineMask) == line &&
                       update.secure == secure;
            }), updates.end());
    }

    void clear() { updates.clear(); }
};

} // namespace gem5::o3
#endif // __CPU_O3_KLP_LOCK_VIEW_HH__
