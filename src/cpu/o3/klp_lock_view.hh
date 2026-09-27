/*
 * Copyright (c) 2026
 * All rights reserved.
 *
 * SPDX-License-Identifier: BSD-3-Clause
 */
#ifndef __CPU_O3_KLP_LOCK_VIEW_HH__
#define __CPU_O3_KLP_LOCK_VIEW_HH__

#include <array>
#include <cassert>
#include <cstdint>

namespace gem5::o3
{

/** Per-execution-attempt lock forwarding. Cache snapshots never mutate it. */
class KlpLockView
{
  public:
    static constexpr unsigned MaxEntries = 8;

  private:
    struct Update
    {
        uint64_t granule;
        bool secure;
        uint64_t seq;
        uint64_t key;
    };
    // Eight scalar bytes can touch at most eight physical lock granules.
    // Only the bounded RV64 path inserts entries into this sidecar.
    std::array<Update, MaxEntries> updates{};
    const unsigned numEntries;
    unsigned numUpdates = 0;
    bool overflow = false;

  public:
    explicit KlpLockView(unsigned entries = MaxEntries) : numEntries(entries)
    {
        assert(entries && entries <= MaxEntries);
    }

    unsigned capacity() const { return numEntries; }

    /** Return whether this is a new, ordered update (including mismatches). */
    bool
    update(uint64_t granule, bool secure, uint64_t seq, uint64_t key)
    {
        for (unsigned i = 0; i < numUpdates; ++i) {
            auto &old = updates[i];
            if (old.granule == granule && old.secure == secure) {
                if (seq <= old.seq)
                    return false;
                old = {granule, secure, seq, key};
                return true;
            }
        }
        if (numUpdates == numEntries) {
            // Defensive fail-closed behavior also applies in gem5.fast.
            overflow = true;
            return false;
        }
        updates[numUpdates++] = {granule, secure, seq, key};
        return true;
    }

    bool
    lookup(uint64_t granule, bool secure, uint64_t &key) const
    {
        for (unsigned i = 0; i < numUpdates; ++i) {
            const auto &update = updates[i];
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
        unsigned retained = 0;
        for (unsigned i = 0; i < numUpdates; ++i) {
            const auto update = updates[i];
            if ((update.granule & lineMask) != line || update.secure != secure)
                updates[retained++] = update;
        }
        numUpdates = retained;
    }

    bool overflowed() const { return overflow; }

    void clear() { numUpdates = 0; overflow = false; }
};

} // namespace gem5::o3
#endif // __CPU_O3_KLP_LOCK_VIEW_HH__
