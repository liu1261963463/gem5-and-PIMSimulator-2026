#include <array>
#include <cstdlib>
#include <iostream>
#include "mem/reliable_pim/pim_ecc.hh"

using namespace gem5::reliable_pim;

static void require(bool ok, const char *message)
{
    if (!ok) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}

// Independent polynomial long division: 128 data bits + eight zeros.
static uint8_t referenceCRC(const std::array<uint8_t, 16> &data)
{
    std::array<unsigned, 136> bits{};
    for (unsigned i = 0; i < 128; ++i)
        bits[i] = (data[i / 8] >> (7 - i % 8)) & 1u;
    for (unsigned i = 0; i < 128; ++i) {
        if (!bits[i])
            continue;
        for (unsigned j = 0; j < 9; ++j)
            bits[i + j] ^= (0x11du >> (8 - j)) & 1u;
    }
    uint8_t remainder = 0;
    for (unsigned i = 128; i < 136; ++i)
        remainder = static_cast<uint8_t>((remainder << 1) | bits[i]);
    return remainder;
}

static void flipBit(Codeword128 &cw, unsigned bit)
{
    if (bit < 128)
        cw.data[bit / 8] ^= static_cast<uint8_t>(1u << (bit % 8));
    else
        cw.parity ^= static_cast<uint16_t>(1u << (bit - 128));
}

int main()
{
    PimEccEngine ecc(EccMode::CRC8);
    std::array<std::array<uint8_t, 16>, 5> inputs{};
    inputs[1].fill(0xff);
    for (unsigned i = 0; i < 16; ++i)
        inputs[2][i] = static_cast<uint8_t>(i);
    inputs[3].fill(0x55);
    inputs[4].fill(0xaa);
    const std::array<uint8_t, 5> expected{0x00, 0xc5, 0x42, 0x43, 0x86};

    unsigned singleTests = 0;
    unsigned doubleTests = 0;
    unsigned dualMisses = 0;
    for (unsigned sample = 0; sample < inputs.size(); ++sample) {
        Codeword128 cw{};
        cw.data = inputs[sample];
        cw.parityBits = 8;
        cw.parity = ecc.encodeCRC8(cw.data);
        require(cw.parity == expected[sample], "known vector");
        require(cw.parity == referenceCRC(cw.data), "polynomial reference");
        require(ecc.checkCRC8(cw), "clean normal CRC");
        require(!ecc.checkFlippedCRC8(cw), "clean flipped check must fail");
        require(ecc.dualDecode(cw) == DecodeResult::NORMAL, "normal decode");

        const auto flipped = ecc.inverted(cw);
        require(!ecc.checkCRC8(flipped), "inverted normal check must fail");
        require(ecc.checkFlippedCRC8(flipped), "inverted flipped check");
        require(ecc.dualDecode(flipped) == DecodeResult::FLIPPED, "flipped decode");
        const auto restored = ecc.inverted(flipped);
        require(restored.data == cw.data && restored.parity == cw.parity,
                "double inversion");

        for (unsigned bit = 0; bit < 136; ++bit) {
            auto bad = cw;
            flipBit(bad, bit);
            require(!ecc.checkCRC8(bad), "single-bit normal detection");
            if (ecc.dualDecode(bad) != DecodeResult::ALERT)
                ++dualMisses;
            bad = flipped;
            flipBit(bad, bit);
            require(!ecc.checkFlippedCRC8(bad), "single-bit flipped detection");
            if (ecc.dualDecode(bad) != DecodeResult::ALERT)
                ++dualMisses;
            singleTests += 2;
        }
        // This guarantee is checked for the ordinary CRC, not dualDecode.
        for (unsigned a = 0; a < 136; ++a) {
            for (unsigned b = a + 1; b < 136; ++b) {
                auto bad = cw;
                flipBit(bad, a);
                flipBit(bad, b);
                require(!ecc.checkCRC8(bad), "two-bit normal detection");
                ++doubleTests;
            }
        }
        auto malformed = cw;
        malformed.parityBits = 16;
        require(ecc.dualDecode(malformed) == DecodeResult::ALERT,
                "reject wrong parity width");
        malformed = cw;
        malformed.parity |= 0x100;
        require(ecc.dualDecode(malformed) == DecodeResult::ALERT,
                "reject nonzero upper parity bits");
    }
    require(dualMisses == 10, "known dual-decoder alias count changed");
    std::cout << "PASS: 5 known/reference vectors\n"
              << "PASS: " << singleTests << " single-bit cases\n"
              << "PASS: " << doubleTests << " ordinary CRC two-bit cases\n"
              << "PASS: normal/flipped decoding and malformed inputs\n"
              << "KNOWN LIMITATION: " << dualMisses
              << " single-bit cases accepted by dualDecode\n";
    return 0;
}
