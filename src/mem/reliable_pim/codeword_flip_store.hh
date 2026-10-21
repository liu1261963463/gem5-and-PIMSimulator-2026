#ifndef __MEM_RELIABLE_PIM_CODEWORD_FLIP_STORE_HH__
#define __MEM_RELIABLE_PIM_CODEWORD_FLIP_STORE_HH__
#include <array>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include "mem/reliable_pim/pim_ecc.hh"
#include "mem/reliable_pim/vrt_fault_model.hh"

namespace gem5
{
namespace reliable_pim
{
struct StoredCodewordSnapshot
{
    Codeword128 physical;
    uint64_t version = 0;
};
struct CodewordReadResult
{
    DualCRC8Result decoded;
    // Present only if the decoder accepts a representation.
    // CRC acceptance does NOT guarantee correct data (known alias exists).
    std::optional<std::array<uint8_t, 16>> logicalData;
    uint64_t version = 0;
};

// Independent functional storage, NOT yet the PIMSimulator Bank backend.
// Holds actual stored bits and a stale-write version; no golden copy/flip flag.
class CodewordFlipStore
{
  public:
    CodewordFlipStore() : ecc(EccMode::CRC8) {}
    void initialize(const CodewordKey &key, const std::array<uint8_t, 16> &data);
    StoredCodewordSnapshot readPhysical(const CodewordKey &key) const;
    CodewordReadResult readForUse(const CodewordKey &key) const;
    unsigned inject(const CodewordKey &key, const VrtFaultModel::WeakMask &mask);

    // Caller supplies logical data already corrected by a backend.
    // Classification alone is insufficient. No implicit recovery is performed.
    // flipAfterCorrection is write policy, never stored as per-word metadata.
    void writeBackCorrected(const CodewordKey &key, uint64_t expectedVersion,
                            const std::array<uint8_t, 16> &correctedLogical,
                            bool flipAfterCorrection);
    uint64_t recoveryWrites() const { return writes; }
    uint64_t flippedWrites() const { return flips; }

  private:
    struct Entry { Codeword128 physical; uint64_t version = 0; };
    PimEccEngine ecc;
    std::unordered_map<CodewordKey, Entry, CodewordKeyHash> storage;
    uint64_t writes = 0;
    uint64_t flips = 0;
    static void validateKey(const CodewordKey &key);
};
} // namespace reliable_pim
} // namespace gem5
#endif
