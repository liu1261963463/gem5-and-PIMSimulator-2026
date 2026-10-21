#include "mem/reliable_pim/pim_ecc.hh"

namespace gem5
    {
    namespace reliable_pim
        {

        PimEccEngine::PimEccEngine(EccMode mode)
            : eccMode(mode)
        {
        }


        uint8_t
        PimEccEngine::encodeCRC8(
            const std::array<uint8_t, 16> &data) const
        {
            constexpr uint8_t poly = 0x1D;

            uint8_t crc = 0x00;

            for (uint8_t byte : data)
            {
                crc ^= byte;

                for (int bit = 0; bit < 8; ++bit)
                {
                    if (crc & 0x80)
                    {
                        crc =
                            static_cast<uint8_t>(
                                (crc << 1) ^ poly);
                    }
                    else
                    {
                        crc =
                            static_cast<uint8_t>(
                                crc << 1);
                    }
                }
            }

            return crc;
        }


        bool
        PimEccEngine::checkCRC8(
            const Codeword128 &cw) const
        {
            // CRC8容器只能包含8位校验值。
            // parity类型虽然是uint16_t，但高8位必须为0。
            if (cw.parityBits != 8 ||
                (cw.parity & 0xff00u) != 0)
            {
                return false;
            }
            const uint8_t expected =
                encodeCRC8(cw.data);

            const uint8_t stored =
                static_cast<uint8_t>(
                    cw.parity & 0xff);

            return expected == stored;
        }

        Codeword128
        PimEccEngine::inverted(
            const Codeword128 &cw) const
        {
            Codeword128 result = cw;

            for (auto &byte : result.data)
            {
                byte = static_cast<uint8_t>(~byte);
            }

            if (result.parityBits == 8)
            {
                result.parity =
                    static_cast<uint8_t>(
                        ~(result.parity & 0xff));
            }
            else
            {
                result.parity =
                    static_cast<uint16_t>(
                        ~result.parity);
            }

            return result;
        }


        bool
        PimEccEngine::checkFlippedCRC8(
            const Codeword128 &cw) const
        {
            // 必须先检查格式，再取反。
            // 否则inverted()可能截掉非法的高8位，
            // 掩盖原始输入格式错误。
            if (cw.parityBits != 8 ||
                (cw.parity & 0xff00u) != 0)
            {
                return false;
            }
            const auto flipped =
                inverted(cw);

            return checkCRC8(flipped);
        }

        DecodeResult
        PimEccEngine::dualDecode(
            const Codeword128 &cw) const
        {
            const bool normalValid =
                checkCRC8(cw);

            const bool flippedValid =
                checkFlippedCRC8(cw);

            // CRC正常，Flip-CRC异常
            if (normalValid && !flippedValid)
            {
                return DecodeResult::NORMAL;
            }

            // CRC异常，Flip-CRC正常
            if (!normalValid && flippedValid)
            {
                return DecodeResult::FLIPPED;
            }

            // 两个都异常
            // 或者两个都判断为正常
            //
            // 均进入ALERT路径
            return DecodeResult::ALERT;
        }
    }
}