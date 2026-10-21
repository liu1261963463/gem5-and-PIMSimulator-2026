#include "mem/reliable_pim/vrt_fault_model.hh"

#include <limits>

namespace gem5
{
namespace reliable_pim
{

        VrtFaultModel::VrtFaultModel(
            double rate,
            uint64_t seed_)
            : vrtRate(rate),
            seed(seed_)
        {
        }


        void
        VrtFaultModel::setRate(double rate)
        {
            vrtRate = rate;
        }


        uint64_t
        VrtFaultModel::hash(
            const CodewordKey &key,
            unsigned bit) const
        {
            uint64_t x = seed;

            x ^= static_cast<uint64_t>(
                CodewordKeyHash{}(key));

            x ^= static_cast<uint64_t>(bit) *
                0x9e3779b97f4a7c15ULL;

            // splitmix64
            x += 0x9e3779b97f4a7c15ULL;

            x =
                (x ^ (x >> 30)) *
                0xbf58476d1ce4e5b9ULL;

            x =
                (x ^ (x >> 27)) *
                0x94d049bb133111ebULL;

            return x ^ (x >> 31);
        }


        bool
        VrtFaultModel::isWeakCell(
            const CodewordKey &key,
            unsigned bit) const
        {
            if (vrtRate <= 0.0)
                return false;

            const long double ratio =
                static_cast<long double>(
                    hash(key, bit)) /
                static_cast<long double>(
                    std::numeric_limits<uint64_t>::max());

            return ratio < vrtRate;
        }

        // void
        // VrtFaultModel::apply(
        //     Codeword128 &cw,
        //     const CodewordKey &key,
        //     const CodewordState &state) const
        // {
        //     for (unsigned bit = 0;
        //         bit < 128;
        //         ++bit)
        //     {
        //         if (!isWeakCell(key, bit))
        //             continue;

        //         const unsigned byte = bit / 8;
        //         const unsigned offset = bit % 8;

        //         bool storedValue =
        //             (cw.data[byte] >> offset) & 0x1;

        //         // 已经Codeword Flip后，
        //         // 物理cell保存的极性发生变化
        //         if (state.physicalFlipped)
        //         {
        //             storedValue = !storedValue;
        //         }

        //         // 第一版模型：1更容易丢失为0
        //         if (storedValue)
        //         {
        //             cw.data[byte] &=
        //                 static_cast<uint8_t>(
        //                     ~(1u << offset));
        //         }
        //     }
        // }
        void
        VrtFaultModel::apply(
            Codeword128 &cw,
            const CodewordKey &key) const
        {
            // cw包含实际存储位。
            // 如果码字已经翻转，翻转必须已经体现在cw中。
            //
            // 当前简化模型只对128个数据位注入故障，
            // 暂不对8个CRC位注入VRT故障。
            for (unsigned bit = 0; bit < 128; ++bit)
            {
                if (!isWeakCell(key, bit))
                {
                    continue;
                }

                const unsigned byte = bit / 8;
                const unsigned offset = bit % 8;

                const uint8_t mask =
                    static_cast<uint8_t>(1u << offset);

                // 弱单元实际存储1时，发生1→0错误。
                if ((cw.data[byte] & mask) != 0)
                {
                    cw.data[byte] &=
                        static_cast<uint8_t>(~mask);
                }
            }
        }
    }
}