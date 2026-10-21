#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "mem/reliable_pim/vrt_fault_model.hh"
using namespace gem5::reliable_pim;
static void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class Exception, class F>
static void throws(F f, const char *message)
{
    bool caught = false;
    try { f(); } catch (const Exception &) { caught = true; }
    require(caught, message);
}
static bool same(const Codeword128 &a, const Codeword128 &b)
{
    return a.data == b.data && a.parity == b.parity && a.parityBits == b.parityBits;
}
int main()
{
    PimEccEngine ecc(EccMode::CRC8);
    CodewordKey key{};
    Codeword128 good{};
    good.data.fill(0xff);
    good.parity = ecc.encodeCRC8(good.data);
    VrtFaultModel zero(0.0, 7), all(1.0, 7), full(1.0, 7, true);
    auto cw = good;
    require(zero.apply(cw, key) == 0 && same(cw, good), "rate zero");
    require(all.weakMask(key).count() == 128, "data-only domain");
    require(full.weakMask(key).count() == 136, "full CRC8 domain");
    require(all.apply(cw, key) == 128, "all data bits decay");
    require(cw.parity == good.parity, "data-only preserves CRC");
    require(all.apply(cw, key) == 0, "repeated injection counts no new errors");
    cw = good;
    require(all.apply(cw, key) == 128, "rewrite restores vulnerable ones");
    std::cout << "PASS: zero/full rates, domains, repeat and rewrite\n";

    // Exhaust all data and parity locations with one physically stored one.
    for (unsigned bit = 0; bit < 136; ++bit) {
        Codeword128 single{};
        if (bit < 128) single.data[bit / 8] = uint8_t(1u << (bit % 8));
        else single.parity = uint16_t(1u << (bit - 128));
        VrtFaultModel::WeakMask mask;
        mask.set(bit);
        require(VrtFaultModel::applyWeakMask(single, mask) == 1, "one transition");
        require(same(single, Codeword128{}), "only selected bit cleared");
        require(VrtFaultModel::applyWeakMask(single, mask) == 0, "zero is stable");
    }
    std::cout << "PASS: all 136 explicit fault locations\n";

    VrtFaultModel a(0.25, 123), b(0.25, 123);
    const auto originalMask = a.weakMask(key);
    auto other = key;
    other.row = 19;
    (void)a.weakMask(other);
    require(originalMask == a.weakMask(key) && originalMask == b.weakMask(key),
            "seed/key/order reproducibility");
    bool locationChanged = false, seedChanged = false;
    for (unsigned i = 1; i < 32; ++i) {
        other.row = i;
        locationChanged |= a.weakMask(other) != originalMask;
        seedChanged |= VrtFaultModel(0.25, 123 + i).weakMask(key) != originalMask;
    }
    require(locationChanged && seedChanged, "location and seed affect mapping");
    a.setRate(0.5);
    require((originalMask & ~a.weakMask(key)).none(), "nested rate thresholds");
    std::cout << "PASS: reproducible location-dependent weak-cell maps\n";

    VrtFaultModel::WeakMask dataMask;
    dataMask.set(0);
    cw = good;
    require(VrtFaultModel::applyWeakMask(cw, dataMask) == 1, "data error");
    require(!ecc.checkCRC8(cw), "ordinary CRC detects injected data error");
    VrtFaultModel::WeakMask crcMask;
    crcMask.set(128); // CRC(16 bytes of FF) = C5, so bit zero is one.
    cw = good;
    require(VrtFaultModel::applyWeakMask(cw, crcMask) == 1, "CRC bit error");
    require(cw.data == good.data && !ecc.checkCRC8(cw), "CRC-bit detection");
    cw = ecc.inverted(good);
    const auto flippedBefore = cw;
    require(all.apply(cw, key) == 0 && same(cw, flippedBefore),
            "stored zero data protected without flip metadata");
    std::cout << "PASS: ordinary CRC checks and physical inversion\n";

    for (double rate : {-0.1, 1.1, std::numeric_limits<double>::infinity(),
                        std::numeric_limits<double>::quiet_NaN()}) {
        throws<std::invalid_argument>([&] { VrtFaultModel invalid(rate, 0); },
                                      "constructor rejects bad rate");
        throws<std::invalid_argument>([&] { a.setRate(rate); },
                                      "setter rejects bad rate");
    }
    throws<std::out_of_range>([&] { all.isWeakCell(key, 136); }, "bit bounds");
    cw = good;
    cw.parityBits = 16;
    throws<std::invalid_argument>([&] { zero.apply(cw, key); }, "CRC width");
    cw = good;
    cw.parity |= 0x100;
    throws<std::invalid_argument>([&] { zero.apply(cw, key); }, "CRC upper bits");
    std::cout << "PASS: invalid parameters rejected\n";
}
