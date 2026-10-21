#include <cstdlib>
#include <iostream>

#include "mem/reliable_pim/pim_ecc.hh"

using namespace gem5::reliable_pim;


static void
require(bool ok, const char *message)
{
    if (!ok)
    {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}


static void
expect(
    const PimEccEngine &ecc,
    const Codeword128 &cw,
    bool normal,
    bool flipped,
    DecodeResult result)
{
    const auto r = ecc.dualDecodeCRC8(cw);

    require(
        r.supported && r.formatValid,
        "valid CRC8 input");

    require(
        r.normalValid == normal,
        "normal branch");

    require(
        r.flippedValid == flipped,
        "flipped branch");

    require(
        r.result == result,
        "decision");

    require(
        ecc.dualDecode(cw) == result,
        "legacy API");
}


int main()
{
    PimEccEngine ecc(EccMode::CRC8);

    // 测试1：正常码字。
    Codeword128 cw{};
    cw.parity = ecc.encodeCRC8(cw.data);

    expect(
        ecc, cw,
        true, false,
        DecodeResult::NORMAL);

    // 测试2：整个码字取反，包括CRC位。
    expect(
        ecc, ecc.inverted(cw),
        false, true,
        DecodeResult::FLIPPED);

    // 测试3：一个可检测的数据位错误。
    auto bad = cw;
    bad.data[0] ^= 1;

    expect(
        ecc, bad,
        false, false,
        DecodeResult::ALERT);

    // 测试4：一个CRC位错误。
    bad = cw;
    bad.parity ^= 1;

    expect(
        ecc, bad,
        false, false,
        DecodeResult::ALERT);

    // 测试5：保留已知误判反例。
    // 这里期待FLIPPED，是记录当前缺陷，
    // 不是认为这个判定符合真实错误状态。
    bad = cw;
    bad.data[15] ^= 0x02;

    expect(
        ecc, bad,
        false, true,
        DecodeResult::FLIPPED);

    std::cout
        << "KNOWN LIMITATION: "
        << "data[15] ^= 0x02 aliases FLIPPED\n";

    // 测试6：错误的CRC宽度。
    bad = cw;
    bad.parityBits = 16;

    auto r = ecc.dualDecodeCRC8(bad);

    require(
        r.supported &&
        !r.formatValid &&
        r.result == DecodeResult::ALERT,
        "bad width");

    // 测试7：CRC8容器高8位不应存在数据。
    bad = cw;
    bad.parity = 0x100;

    r = ecc.dualDecodeCRC8(bad);

    require(
        !r.formatValid &&
        r.result == DecodeResult::ALERT,
        "bad parity upper bits");

    // 测试8：禁止其他ECC模式误用CRC8双解码。
    for (auto mode :
         {EccMode::NONE, EccMode::CRC16, EccMode::SEC})
    {
        const auto unsupported =
            PimEccEngine(mode).dualDecodeCRC8(cw);

        require(
            !unsupported.supported &&
            unsupported.result == DecodeResult::ALERT,
            "unsupported mode");
    }

    std::cout
        << "PASS: dual CRC8 branches, "
        << "validation, compatibility\n";

    return 0;
}