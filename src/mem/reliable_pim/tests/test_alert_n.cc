#include <cstdlib>
#include <iostream>
#include "mem/reliable_pim/pim_alert.hh"
#include "mem/reliable_pim/vrt_fault_model.hh"
using namespace gem5::reliable_pim;
static void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
int main()
{
    PimAlertLatch alarm;
    require(alarm.levelHigh() && !alarm.pending(), "initial high");
    require(!alarm.observe(false) && alarm.levelHigh(), "normal does not assert");
    require(alarm.observe(true) && !alarm.levelHigh(), "first fault asserts");
    require(!alarm.observe(true) && alarm.pending(), "repeat fault has no new edge");
    require(!alarm.observe(false) && alarm.pending(), "success cannot clear fault");
    alarm.releaseAfterRecovery();
    require(alarm.levelHigh() && !alarm.pending(), "explicit release");
    require(alarm.observe(true), "new episode asserts again");
    std::cout << "PASS: active-low, sticky and repeated alerts\n";

    PimEccEngine ecc(EccMode::CRC8);
    Codeword128 good{};
    good.data.fill(0xff);
    good.parity = ecc.encodeCRC8(good.data);
    auto stored = good;
    VrtFaultModel::WeakMask mask;
    mask.set(0);
    require(VrtFaultModel::applyWeakMask(stored, mask) == 1, "inject one bit");
    const auto bad = ecc.dualDecodeCRC8(stored);
    require(bad.supported && bad.formatValid && bad.result == DecodeResult::ALERT,
            "selected fault detected");
    PimAlertLatch pipeline;
    require(pipeline.observe(bad.result == DecodeResult::ALERT), "decoder drives alert");
    const auto clean = ecc.dualDecodeCRC8(good);
    require(!pipeline.observe(clean.result == DecodeResult::ALERT) && pipeline.pending(),
            "later clean codeword must not clear earlier alarm");
    // A test fixture supplies a known correct word; this is NOT rank ECC.
    stored = good;
    require(ecc.checkCRC8(stored), "test fixture restored word");
    pipeline.releaseAfterRecovery();
    require(pipeline.levelHigh(), "test-only release after restore");
    std::cout << "PASS: VRT -> dual CRC -> ALERT_n (standalone)\n";

    PimAlertLatch first, second;
    const auto aggregateHigh = [&] { return first.levelHigh() && second.levelHigh(); };
    first.observe(true); second.observe(true);
    first.releaseAfterRecovery();
    require(!aggregateHigh(), "other pending source keeps line low");
    second.releaseAfterRecovery();
    require(aggregateHigh(), "all sources released");
    std::cout << "PASS: multiple pending sources\n";

    // Known dual-decoder miss must remain observable, not oracle-corrected.
    Codeword128 alias{};
    alias.parity = ecc.encodeCRC8(alias.data);
    alias.data[15] ^= 0x02;
    PimAlertLatch missed;
    missed.observe(ecc.dualDecode(alias) == DecodeResult::ALERT);
    require(missed.levelHigh(), "known alias behavior changed");
    std::cout << "KNOWN LIMITATION: dual-decoder alias does not assert ALERT_n\n";
}
