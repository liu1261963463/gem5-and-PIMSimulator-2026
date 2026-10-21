#ifndef __MEM_RELIABLE_PIM_VRT_FAULT_MODEL_HH__
#define __MEM_RELIABLE_PIM_VRT_FAULT_MODEL_HH__

#include <bitset>
#include <cstdint>
#include "mem/reliable_pim/pim_ecc.hh"

namespace gem5
{
namespace reliable_pim
{

// Fixed weak-cell, one-way retention-fault approximation.
// No time-dependent VRT state transitions are modeled here.
class VrtFaultModel
{
  public:
    using WeakMask = std::bitset<136>;

    // Legacy two-argument construction still selects data-only faults.
    VrtFaultModel(double rate, uint64_t seed, bool include_crc = false);
    void setRate(double rate);
    bool isWeakCell(const CodewordKey &key, unsigned bit) const;
    WeakMask weakMask(const CodewordKey &key) const;

    // cw contains physical stored bits, not decoded logical data.
    // Return the number of actual 1->0 transitions in this invocation.
    unsigned apply(Codeword128 &cw, const CodewordKey &key) const;

    // Deterministic test injection; explicit mask overrides rate/domain.
    // Bits 0..127: data, LSB-first per byte. Bits 128..135: CRC, LSB-first.
    static unsigned applyWeakMask(Codeword128 &cw, const WeakMask &mask);

  private:
    double vrtRate;
    uint64_t seed;
    bool includeCrc;
    uint64_t hash(const CodewordKey &key, unsigned bit) const;
};

} // namespace reliable_pim
} // namespace gem5
#endif
