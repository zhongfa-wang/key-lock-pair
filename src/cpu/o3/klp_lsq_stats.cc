#include "cpu/o3/klp_lsq_stats.hh"

#include <sstream>

namespace gem5 { namespace o3 {

KlpLsqStats::KlpLsqStats(statistics::Group *parent)
    : statistics::Group(parent, "klpDiag"),
      ADD_STAT(checkOutcome, statistics::units::Count::get(),
               "Exclusive first cache-check outcomes; includes uncondi and later squashed loads"),
      ADD_STAT(heldReason, statistics::units::Count::get(),
               "Exclusive cause on first data-ready hold in this statistics epoch"),
      ADD_STAT(stlfBlockReason, statistics::units::Count::get(),
               "Exclusive key-blocked STLF attempt reasons"),
      ADD_STAT(stlfBlockedLoads, statistics::units::Count::get(),
               "Distinct dynamic loads first key-blocked in this statistics epoch"),
      ADD_STAT(stlfWakeReason, statistics::units::Count::get(),
               "Key-blocked STLF wake attempts by reason"),
      ADD_STAT(stlfBlockToWake, statistics::units::Cycle::get(),
               "Each paired STLF block-to-wake interval; overlaps across loads"),
      ADD_STAT(stlfBlockToSquash, statistics::units::Cycle::get(),
               "Outstanding paired STLF wait terminated by squash"),
      ADD_STAT(stlfRetryToRead, statistics::units::Cycle::get(),
               "Wake/retry notification to next LSQ read, including AGU and translation"),
      ADD_STAT(stlfReadsAfterRetry, statistics::units::Count::get(),
               "LSQ reads paired with a previous key-blocked retry notification"),
      ADD_STAT(stlfRetryOutcome, statistics::units::Count::get(),
               "Disposition of the first read after each key-blocked retry"),
      ADD_STAT(stlfPairs, statistics::units::Count::get(),
               "Load/store PC and reason for blocked STLF attempts; full-width hex PC labels"),
      ADD_STAT(violationKind, statistics::units::Count::get(),
               "LSQ violation detections split by trigger kind and exact byte overlap"),
      ADD_STAT(violationVictimFlags, statistics::units::Count::get(),
               "Victim mask: held=1, previously STLF blocked in epoch=2, executed=4, uncondi=8, hasRequest=16"),
      ADD_STAT(violationPairs, statistics::units::Count::get(),
               "Trigger/victim PC pairs and exact/coarse overlap; not necessarily an accepted squash"),
      ADD_STAT(snoopReexec, statistics::units::Count::get(),
               "Snoop checks assigning a ReExec fault to a load"),
      ADD_STAT(snoopReexecHeld, statistics::units::Count::get(),
               "Snoop ReExec assignments whose victim holds KLP data"),
      ADD_STAT(squashedHeld, statistics::units::Count::get(),
               "Data-ready KLP loads removed from LQ by squash"),
      ADD_STAT(heldToSquash, statistics::units::Cycle::get(),
               "Paired data-ready hold to LQ squash; excludes waits begun before reset"),
      ADD_STAT(squashedAfterStlfBlock, statistics::units::Count::get(),
               "Loads removed by squash after a key-blocked STLF in this epoch")
{
    const char *outcomes[] = {"pass", "none", "ambiguous", "invalid",
                             "mismatch", "invalid_and_mismatch"};
    checkOutcome.init(6).flags(statistics::nozero);
    heldReason.init(6).flags(statistics::nozero);
    for (unsigned i = 0; i < 6; ++i) {
        checkOutcome.subname(i, outcomes[i]);
        heldReason.subname(i, outcomes[i]);
    }
    stlfBlockReason.init(4).flags(statistics::nozero);
    const char *reasons[] = {"load_unknown", "store_unknown", "both_unknown",
                            "known_mismatch"};
    for (unsigned i = 0; i < 4; ++i)
        stlfBlockReason.subname(i, reasons[i]);
    stlfWakeReason.init(2).subname(0, "uncondi").subname(1, "store_complete");
    stlfRetryOutcome.init(5).subname(0, "forwarded").subname(1, "cache_path")
        .subname(2, "key_blocked_again").subname(3, "partial_forward_block")
        .subname(4, "special_path").flags(statistics::nozero);
    stlfBlockToWake.init(32);
    stlfBlockToSquash.init(32);
    stlfRetryToRead.init(32);
    heldToSquash.init(32);
    violationKind.init(4).subname(0, "store_byte_overlap")
        .subname(1, "store_coarse_only").subname(2, "load_byte_overlap")
        .subname(3, "load_coarse_only").flags(statistics::nozero);
    violationVictimFlags.init(32).flags(statistics::nozero);
    stlfPairs.init(PairLimit + 1).subname(PairLimit, "overflow")
        .flags(statistics::nozero);
    violationPairs.init(PairLimit + 1).subname(PairLimit, "overflow")
        .flags(statistics::nozero);
}

void
KlpLsqStats::samplePair(statistics::Vector &stat,
                       std::map<std::string, unsigned> &ids,
                       const std::string &key)
{
    auto found = ids.find(key);
    if (found != ids.end()) {
        ++stat[found->second];
    } else if (ids.size() < PairLimit) {
        const unsigned index = ids.size();
        ids.emplace(key, index);
        stat.subname(index, key);
        ++stat[index];
    } else {
        ++stat[PairLimit];
    }
}

void
KlpLsqStats::recordStlfPair(Addr load, Addr store, unsigned reason)
{
    std::ostringstream label;
    label << "load_0x" << std::hex << load << "_store_0x" << store
          << "_reason_" << std::dec << reason;
    samplePair(stlfPairs, stlfPairIds, label.str());
}

void
KlpLsqStats::recordViolation(Addr trigger, Addr victim, bool load_trigger,
                            bool byte_overlap, unsigned victim_flags)
{
    const unsigned kind = (load_trigger ? 2 : 0) + (byte_overlap ? 0 : 1);
    ++violationKind[kind];
    ++violationVictimFlags[victim_flags];
    std::ostringstream label;
    label << (load_trigger ? "load_0x" : "store_0x") << std::hex << trigger
          << "_victim_0x" << victim
          << (byte_overlap ? "_byte_overlap" : "_coarse_only");
    samplePair(violationPairs, violationPairIds, label.str());
}

void
KlpLsqStats::resetStats()
{
    statistics::Group::resetStats();
    for (unsigned i = 0; i < stlfPairIds.size(); ++i)
        stlfPairs.subname(i, "");
    for (unsigned i = 0; i < violationPairIds.size(); ++i)
        violationPairs.subname(i, "");
    stlfPairIds.clear();
    violationPairIds.clear();
}

} } // namespace gem5::o3
