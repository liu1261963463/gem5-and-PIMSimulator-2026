#include "mem/reliable_pim/codeword_flip_store.hh"
#include <limits>
#include <stdexcept>

namespace gem5
{
namespace reliable_pim
{
void CodewordFlipStore::validateKey(const CodewordKey &key)
{
    if (key.bank >= 32 || key.subBlock >= 2)
        throw std::invalid_argument("CodewordFlipStore requires bank<32 and subBlock<2");
}
void CodewordFlipStore::initialize(const CodewordKey &key,
                                  const std::array<uint8_t, 16> &data)
{
    validateKey(key);
    Entry entry;
    entry.physical.data = data;
    entry.physical.parityBits = 8;
    entry.physical.parity = ecc.encodeCRC8(data);
    if (!storage.emplace(key, entry).second)
        throw std::logic_error("CodewordFlipStore refuses duplicate initialization");
}
StoredCodewordSnapshot CodewordFlipStore::readPhysical(const CodewordKey &key) const
{
    validateKey(key);
    const auto &entry = storage.at(key); // Unknown addresses never become zero words.
    return {entry.physical, entry.version};
}
CodewordReadResult CodewordFlipStore::readForUse(const CodewordKey &key) const
{
    const auto snapshot = readPhysical(key);
    CodewordReadResult result;
    result.version = snapshot.version;
    result.decoded = ecc.dualDecodeCRC8(snapshot.physical);
    if (!result.decoded.supported || !result.decoded.formatValid)
        throw std::logic_error("CodewordFlipStore invalid CRC8 storage format");
    if (result.decoded.result == DecodeResult::NORMAL)
        result.logicalData = snapshot.physical.data;
    else if (result.decoded.result == DecodeResult::FLIPPED)
        result.logicalData = ecc.inverted(snapshot.physical).data;
    // ALERT: logicalData remains empty; caller must not consume the data.
    return result;
}
unsigned CodewordFlipStore::inject(const CodewordKey &key,
                                  const VrtFaultModel::WeakMask &mask)
{
    validateKey(key);
    auto &entry = storage.at(key);
    auto damaged = entry.physical;
    const unsigned changed = VrtFaultModel::applyWeakMask(damaged, mask);
    if (changed != 0) {
        if (entry.version == std::numeric_limits<uint64_t>::max())
            throw std::overflow_error("Codeword version exhausted");
        entry.physical = damaged;
        ++entry.version;
    }
    return changed;
}
void CodewordFlipStore::writeBackCorrected(const CodewordKey &key,
    uint64_t expectedVersion, const std::array<uint8_t, 16> &correctedLogical,
    bool flipAfterCorrection)
{
    validateKey(key);
    auto &entry = storage.at(key);
    if (entry.version != expectedVersion)
        throw std::logic_error("Stale correction writeback: storage changed since read");
    if (entry.version == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("Codeword version exhausted");

    // Encode the CORRECTED logical word, never the damaged stored word.
    Codeword128 rebuilt;
    rebuilt.data = correctedLogical;
    rebuilt.parityBits = 8;
    rebuilt.parity = ecc.encodeCRC8(correctedLogical);
    if (flipAfterCorrection)
        rebuilt = ecc.inverted(rebuilt); // Flip all 128 data + 8 CRC bits.

    // Atomic update in this functional backend only; no DRAM latency modeled.
    entry.physical = rebuilt;
    ++entry.version;
    ++writes;
    if (flipAfterCorrection)
        ++flips;
}
} // namespace reliable_pim
} // namespace gem5
