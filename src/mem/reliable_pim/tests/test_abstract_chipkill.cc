#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "mem/reliable_pim/abstract_chipkill.hh"
#include "mem/reliable_pim/rank_correction.hh"
using namespace gem5::reliable_pim;
static void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class F> static void rejects(F f)
{
    bool caught = false;
    try { f(); } catch (const std::invalid_argument &) { caught = true; }
    require(caught, "invalid input rejected");
}
using Word = std::array<uint16_t, 4>;
// Test-only oracle. A real syndrome decoder does not possess golden data.
static std::vector<unsigned> differences(const Word &received, const Word &golden)
{
    std::vector<unsigned> errors(4, 0);
    for (unsigned chip = 0; chip < 4; ++chip) {
        unsigned delta = received[chip] ^ golden[chip];
        while (delta) { errors[chip] += delta & 1u; delta >>= 1; }
    }
    return errors;
}
int main()
{
    // Synthetic fixture; NOT the geometry of the user's HBM platform.
    AbstractChipkill model(4, 16);
    require(model.classify({0,0,0,0}).outcome == AbstractChipkillOutcome::CLEAN, "clean");
    for (unsigned chip = 0; chip < 4; ++chip) {
        for (unsigned bits = 1; bits <= 16; ++bits) {
            std::vector<unsigned> errors(4, 0); errors[chip] = bits;
            const auto r = model.classify(errors);
            require(r.outcome == AbstractChipkillOutcome::SINGLE_CHIP_CORRECTABLE &&
                    r.affectedChips == 1 && r.erroneousBits == bits, "single chip");
        }
    }
    for (unsigned a = 0; a < 4; ++a)
        for (unsigned b = a + 1; b < 4; ++b) {
            std::vector<unsigned> errors(4,0); errors[a] = errors[b] = 1;
            require(model.classify(errors).outcome ==
                    AbstractChipkillOutcome::OUTSIDE_MODEL_CAPABILITY, "two chips");
        }
    require(model.classify({16,16,16,16}).erroneousBits == 64, "full fault summary");
    rejects([] { AbstractChipkill bad(0,16); });
    rejects([] { AbstractChipkill bad(4,0); });
    rejects([&] { model.classify({0,0,0}); });
    rejects([&] { model.classify({17,0,0,0}); });
    std::cout << "PASS: clean, single-chip, multi-chip and invalid inputs\n";

    // Same number of errors, different chip distributions.
    require(model.classify({2,0,0,0}).outcome != model.classify({1,1,0,0}).outcome,
            "chip distribution matters, not just bit count");
    // Two distinct codewords are evaluated independently, never merged.
    require(model.classify({1,0,0,0}).outcome == AbstractChipkillOutcome::SINGLE_CHIP_CORRECTABLE &&
            model.classify({0,1,0,0}).outcome == AbstractChipkillOutcome::SINGLE_CHIP_CORRECTABLE,
            "per-codeword scope");
    std::cout << "PASS: per-codeword scope and chip distribution\n";

    const Word golden{0x1234,0x5678,0xabcd,0xef01};
    Word received = golden; received[2] ^= 0x00ff;
    const Word damaged = received;
    const auto result = model.classify(differences(received, golden));
    require(received == damaged, "classification does not modify data");
    require(result.outcome == AbstractChipkillOutcome::SINGLE_CHIP_CORRECTABLE, "model capacity");
    RankCorrectionEngine fsm;
    fsm.start(1, {{0,0,2,10,4}}, 0);
    fsm.readComplete(fsm.token(), true); // Test fixture only, not a DRAM response.
    // Explicit ideal repair in the TEST, not inside the capability model.
    Word writeBuffer = golden;
    require(writeBuffer == golden, "test oracle supplied correct write buffer");
    fsm.decodeComplete(fsm.token(), RankDecodeStatus::CORRECTED);
    require(fsm.state() == CorrectionState::WRITEBACK, "repair is not writeback");
    received = writeBuffer; // Test-only memory write.
    fsm.writebackComplete(fsm.token(), true);
    require(fsm.state() == CorrectionState::READY_RETRY, "writeback is not retry completion");
    std::cout << "PASS: explicit test-oracle repair and FSM writeback sequencing\n";

    received = golden; received[0] ^= 1; received[3] ^= 1;
    const auto multi = model.classify(differences(received, golden));
    require(multi.outcome == AbstractChipkillOutcome::OUTSIDE_MODEL_CAPABILITY, "multi fault");
    RankCorrectionEngine failed;
    failed.start(2, {{0,0,2,10,4}}, 0);
    failed.readComplete(failed.token(), true);
    // Policy of this idealized model: outside capability is treated as detected UCE.
    failed.decodeComplete(failed.token(), RankDecodeStatus::UNCORRECTABLE);
    require(failed.state() == CorrectionState::FAILED &&
            failed.context().writtenTargets == 0 && received != golden, "no false repair");
    std::cout << "PASS: outside-capability faults fail without writeback\n";
    std::cout << "NOTE: idealized oracle model; no real ECC, chip mapping or SDC evaluation\n";
}
