#include "mem/reliable_pim/vrt_fault_model.hh"

#include <cmath>
#include <stdexcept>

namespace gem5
{
namespace reliable_pim
{
namespace
{
uint64_t mix64(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    return x ^ (x >> 31);
}
}

VrtFaultModel::VrtFaultModel(double rate, uint64_t seed_, bool include_crc)
    : vrtRate(0.0), seed(seed_), includeCrc(include_crc)
{
    setRate(rate);
}

void VrtFaultModel::setRate(double rate)
{
    if (!std::isfinite(rate) || rate < 0.0 || rate > 1.0)
        throw std::invalid_argument("VRT weak-cell rate must be finite and in [0,1]");
    vrtRate = rate;
}

uint64_t VrtFaultModel::hash(const CodewordKey &key, unsigned bit) const
{
    // Fixed-width mapping, independent of size_t and access order.
    uint64_t x = mix64(seed);
    const uint64_t fields[] = {
        key.channel, key.rank, key.chip, key.bank,
        key.row, key.column, key.subBlock, bit
    };
    for (uint64_t field : fields)
        x = mix64(x ^ field);
    return x;
}

bool VrtFaultModel::isWeakCell(const CodewordKey &key, unsigned bit) const
{
    if (bit >= 136)
        throw std::out_of_range("CRC8 codeword bit index must be below 136");
    if (!includeCrc && bit >= 128)
        return false;
    if (vrtRate == 0.0)
        return false;
    if (vrtRate == 1.0)
        return true;
    // Exactly representable uniform grid in [0,1), with 53 random bits.
    const double u = static_cast<double>(hash(key, bit) >> 11) * 0x1.0p-53;
    return u < vrtRate;
}

VrtFaultModel::WeakMask
VrtFaultModel::weakMask(const CodewordKey &key) const
{
    WeakMask mask;
    for (unsigned bit = 0; bit < 136; ++bit)
        mask.set(bit, isWeakCell(key, bit));
    return mask;
}

unsigned VrtFaultModel::apply(Codeword128 &cw, const CodewordKey &key) const
{
    return applyWeakMask(cw, weakMask(key));
}

unsigned VrtFaultModel::applyWeakMask(Codeword128 &cw, const WeakMask &mask)
{
    if (cw.parityBits != 8 || (cw.parity & 0xff00u) != 0)
        throw std::invalid_argument("VRT injector requires a valid CRC8 container");
    unsigned changed = 0;
    for (unsigned bit = 0; bit < 136; ++bit) {
        if (!mask.test(bit))
            continue;
        if (bit < 128) {
            const unsigned byte = bit / 8;
            const uint8_t m = static_cast<uint8_t>(1u << (bit % 8));
            if ((cw.data[byte] & m) != 0) {
                cw.data[byte] &= static_cast<uint8_t>(~m);
                ++changed;
            }
        } else {
            const uint16_t m = static_cast<uint16_t>(1u << (bit - 128));
            if ((cw.parity & m) != 0) {
                cw.parity &= static_cast<uint16_t>(~m);
                ++changed;
            }
        }
    }
    return changed;
}

} // namespace reliable_pim
} // namespace gem5
