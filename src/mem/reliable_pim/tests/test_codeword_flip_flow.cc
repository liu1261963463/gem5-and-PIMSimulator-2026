#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "mem/reliable_pim/codeword_flip_store.hh"
#include "mem/reliable_pim/abstract_chipkill.hh"
#include "mem/reliable_pim/rank_correction.hh"
#include "mem/reliable_pim/pim_alert.hh"
using namespace gem5::reliable_pim;
static void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class E, class F> static void rejects(F f, const char *message)
{
    bool caught = false;
    try { f(); } catch (const E &) { caught = true; }
    require(caught, message);
}
static bool same(const Codeword128 &a, const Codeword128 &b)
{ return a.data == b.data && a.parity == b.parity && a.parityBits == b.parityBits; }
int main()
{
    CodewordFlipStore store;
    PimEccEngine ecc(EccMode::CRC8);
    CodewordKey key{};
    key.bank = 2; key.row = 10; key.column = 4; key.chip = 2;
    auto neighbor = key; neighbor.subBlock = 1;
    std::array<uint8_t,16> golden; golden.fill(0xff);
    std::array<uint8_t,16> other; other.fill(0x55);
    store.initialize(key, golden); store.initialize(neighbor, other);
    const auto initial = store.readPhysical(key);
    const auto untouched = store.readPhysical(neighbor);
    require(store.readForUse(key).logicalData.value() == golden, "initial read");
    rejects<std::logic_error>([&] { store.initialize(key, golden); }, "no silent reinitialization");
    auto missing = key; missing.row = 999;
    rejects<std::out_of_range>([&] { store.readForUse(missing); }, "no fabricated zero word");

    VrtFaultModel::WeakMask weak; weak.set(0);
    require(store.inject(key, weak) == 1, "physical VRT injection");
    const auto badRead = store.readForUse(key);
    require(badRead.decoded.result == DecodeResult::ALERT && !badRead.logicalData,
            "bad data not delivered");
    PimAlertLatch alarm;
    alarm.observe(badRead.decoded.result == DecodeResult::ALERT);
    require(alarm.pending(), "alarm asserted");

    // Synthetic geometry, not actual HBM mapping. Error truth known by test.
    AbstractChipkill capacity(4,136);
    require(capacity.classify({0,0,1,0}).outcome ==
            AbstractChipkillOutcome::SINGLE_CHIP_CORRECTABLE, "abstract capacity");
    RankCorrectionEngine fsm;
    fsm.start(1, {{0,0,2,10,4}}, 0);
    fsm.readComplete(fsm.token(), true); // Functional test read has completed.
    const auto correctedLogical = golden; // Explicit test oracle, NOT real ECC.
    fsm.decodeComplete(fsm.token(), RankDecodeStatus::CORRECTED);
    require(fsm.state() == CorrectionState::WRITEBACK && store.flippedWrites() == 0,
            "no flip before writeback");
    store.writeBackCorrected(key, badRead.version, correctedLogical, true);
    // Only acknowledge after the functional store commit succeeds.
    fsm.writebackComplete(fsm.token(), true);
    const auto physical = store.readPhysical(key);
    require(same(physical.physical, ecc.inverted(initial.physical)), "all 136 bits inverted");
    require(same(store.readPhysical(neighbor).physical, untouched.physical) &&
            store.readPhysical(neighbor).version == untouched.version, "neighbor unchanged");
    require(store.recoveryWrites() == 1 && store.flippedWrites() == 1, "commit-only counters");
    require(store.inject(key, weak) == 0, "same weak cell now stores zero");
    require(store.readPhysical(key).version == physical.version, "no-op injection preserves version");
    fsm.retryIssued(fsm.token());
    const auto reread = store.readForUse(key);
    require(reread.decoded.result == DecodeResult::FLIPPED &&
            reread.logicalData.value() == golden, "decode to original logical data");
    // Test retry is a read, not execution of a PIM arithmetic kernel.
    fsm.retryComplete(fsm.token(), true);
    require(fsm.state() == CorrectionState::COMPLETE, "test recovery complete");
    require(alarm.pending(), "no implicit alarm release");
    alarm.releaseAfterRecovery(); // Test controller explicitly ends episode.
    std::cout << "PASS: fault -> alert -> abstract repair -> physical flip -> reread\n";

    // Fresh logical encoding prevents accidentally toggling an already flipped word.
    store.writeBackCorrected(key, reread.version, golden, true);
    require(same(store.readPhysical(key).physical, physical.physical), "no accidental double flip");
    // Disabled flip policy writes normal representation.
    store.writeBackCorrected(key, store.readPhysical(key).version, golden, false);
    require(same(store.readPhysical(key).physical, initial.physical), "normal write policy");
    const auto stale = store.readPhysical(key);
    store.inject(key, weak);
    const auto damaged = store.readPhysical(key);
    const auto writes = store.recoveryWrites();
    rejects<std::logic_error>([&] {
        store.writeBackCorrected(key, stale.version, golden, true);
    }, "stale recovery rejected");
    require(same(store.readPhysical(key).physical, damaged.physical) &&
            store.recoveryWrites() == writes, "failed commit preserves data and counters");
    std::cout << "PASS: isolation, version checks and explicit write policy\n";

    CodewordKey mixedKey{}; mixedKey.row = 20;
    std::array<uint8_t,16> mixed{}; mixed[0] = 1;
    store.initialize(mixedKey, mixed);
    VrtFaultModel::WeakMask two; two.set(0); two.set(1);
    require(store.inject(mixedKey, two) == 1, "first vulnerable bit");
    store.writeBackCorrected(mixedKey, store.readPhysical(mixedKey).version, mixed, true);
    require(store.inject(mixedKey, two) == 1, "previous zero weak cell exposed by inversion");
    require(!store.readForUse(mixedKey).logicalData, "new detected error blocks output");
    std::cout << "PASS: flipping can expose another weak cell\n";

    CodewordKey aliasKey{}; aliasKey.row = 30;
    store.initialize(aliasKey, golden);
    VrtFaultModel::WeakMask aliasMask; aliasMask.set(121);
    require(store.inject(aliasKey, aliasMask) == 1, "known one-way alias injection");
    const auto alias = store.readForUse(aliasKey);
    require(alias.decoded.result == DecodeResult::FLIPPED &&
            alias.logicalData.has_value() && alias.logicalData.value() != golden,
            "known dual decoder SDC counterexample remains visible");
    std::cout << "KNOWN LIMITATION: dual CRC can accept incorrect logical data\n";
    std::cout << "NOTE: functional store + test oracle; no real Rank ECC/Bank/PIM retry\n";
}
