#ifndef __MEM_RELIABLE_PIM_PIM_ECC_HH__
#define __MEM_RELIABLE_PIM_PIM_ECC_HH__

#include <array>
#include <cstdint>

#include "mem/reliable_pim/reliable_pim_types.hh"

namespace gem5
{
namespace reliable_pim
{

struct Codeword128
{
    // 128 bit = 16 bytes
    std::array<uint8_t, 16> data{};

    // CRC8使用低8位
    // CRC16使用完整16位
    uint16_t parity = 0;

    unsigned parityBits = 8;
};

// 双CRC8解码的详细结果。
// NORMAL/FLIPPED表示校验判定，不是无错误的绝对保证。
struct DualCRC8Result
{
    // 当前Engine是否配置为CRC8模式。
    bool supported = false;

    // 是否为合法的8位CRC容器。
    bool formatValid = false;

    // true表示对应分支校验通过。
    bool normalValid = false;
    bool flippedValid = false;

    // 默认采用保守结果。
    DecodeResult result = DecodeResult::ALERT;
};

class PimEccEngine
{
  public:

    explicit PimEccEngine(EccMode mode);

    // -------------------------
    // 编码
    // -------------------------
    uint8_t
    encodeCRC8(
        const std::array<uint8_t, 16> &data) const;

    // 第一阶段可以暂时不实现CRC16
    uint16_t
    encodeCRC16(
        const std::array<uint8_t, 16> &data) const;

    // -------------------------
    // 检查
    // -------------------------
    bool
    checkCRC8(const Codeword128 &cw) const;

    bool
    checkFlippedCRC8(
        const Codeword128 &cw) const;

    // 返回两路校验状态和最终判定。
    DualCRC8Result
    dualDecodeCRC8(
        const Codeword128 &cw) const;
        
    DecodeResult
    dualDecode(
        const Codeword128 &cw) const;

    // -------------------------
    // 整个codeword取反
    // -------------------------
    Codeword128
    inverted(
        const Codeword128 &cw) const;

  private:

    EccMode eccMode;
};

} // namespace reliable_pim
} // namespace gem5

#endif