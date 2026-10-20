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