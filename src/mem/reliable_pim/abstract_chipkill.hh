#ifndef __MEM_RELIABLE_PIM_ABSTRACT_CHIPKILL_HH__
#define __MEM_RELIABLE_PIM_ABSTRACT_CHIPKILL_HH__

#include <cstdint>
#include <vector>

namespace gem5
{
namespace reliable_pim
{
// Capacity classification, NOT a real ECC decoder result.
enum class AbstractChipkillOutcome
{
    CLEAN,
    SINGLE_CHIP_CORRECTABLE,
    OUTSIDE_MODEL_CAPABILITY
};
struct AbstractChipkillResult
{
    AbstractChipkillOutcome outcome = AbstractChipkillOutcome::CLEAN;
    unsigned affectedChips = 0;
    uint64_t erroneousBits = 0;
};

// Idealized single-chip correction for ONE rank codeword.
// Geometry must include every chip slice protected by that codeword.
// Input counts are simulator ground truth, not hardware-visible syndromes.
// No encoding, decoding, data repair, SDC estimation or timing is performed.
class AbstractChipkill
{
  public:
    AbstractChipkill(unsigned chipCount, unsigned bitsPerChipPerCodeword);
    AbstractChipkillResult classify(
        const std::vector<unsigned> &erroneousBitsPerChip) const;

  private:
    unsigned chips;
    unsigned bitsPerChip;
};
} // namespace reliable_pim
} // namespace gem5
#endif
