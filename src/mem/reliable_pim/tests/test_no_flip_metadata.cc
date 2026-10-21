#include <cstdlib>
#include <iostream>
#include "mem/reliable_pim/pim_ecc.hh"
#include "mem/reliable_pim/vrt_fault_model.hh"
using namespace gem5::reliable_pim;
static void require(bool ok, const char *message)
{
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
int main()
{
    PimEccEngine ecc(EccMode::CRC8);
    CodewordKey key{};
    Codeword128 logical{};
    logical.data.fill(0xff);
    logical.parity = ecc.encodeCRC8(logical.data);

    // Only actual codeword bits are inverted; no state table is involved.
    auto stored = ecc.inverted(logical);
    require(ecc.dualDecode(stored) == DecodeResult::FLIPPED,
            "infer clean inverted representation from CRC");
    const auto originalStored = stored;
    VrtFaultModel allWeak(1.0, 1);
    for (unsigned bit = 0; bit < 128; ++bit)
        require(allWeak.isWeakCell(key, bit), "test weak-cell setup");
    allWeak.apply(stored, key);
    require(stored.data == originalStored.data &&
            stored.parity == originalStored.parity,
            "stored zero bits must not turn into ones");
    const auto decoded = ecc.inverted(stored);
    require(decoded.data == logical.data && decoded.parity == logical.parity,
            "clean flipped representation restores original codeword");

    auto damaged = logical;
    allWeak.apply(damaged, key);
    for (auto byte : damaged.data)
        require(byte == 0, "stored ones must decay to zero");
    require(damaged.parity == logical.parity,
            "data-only fault model must preserve CRC bits");
    require(!ecc.checkCRC8(damaged), "detect this damaged codeword");

    VrtFaultModel noFaults(0.0, 1);
    auto untouched = logical;
    noFaults.apply(untouched, key);
    require(untouched.data == logical.data &&
            untouched.parity == logical.parity, "zero-rate identity");
    std::cout << "PASS: stored-bit inversion and VRT without flip metadata\n";
}
