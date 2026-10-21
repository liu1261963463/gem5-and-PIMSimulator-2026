#include "mem/reliable_pim/abstract_chipkill.hh"
#include <stdexcept>

namespace gem5
{
namespace reliable_pim
{
AbstractChipkill::AbstractChipkill(
    unsigned chipCount, unsigned bitsPerChipPerCodeword)
    : chips(chipCount), bitsPerChip(bitsPerChipPerCodeword)
{
    if (chips < 2 || bitsPerChip == 0)
        throw std::invalid_argument(
            "AbstractChipkill requires at least two chips and nonzero slice width");
}

AbstractChipkillResult
AbstractChipkill::classify(const std::vector<unsigned> &errors) const
{
    if (errors.size() != chips)
        throw std::invalid_argument(
            "AbstractChipkill requires one error count per chip in ONE codeword");

    AbstractChipkillResult result;
    for (unsigned count : errors) {
        if (count > bitsPerChip)
            throw std::invalid_argument(
                "AbstractChipkill error count exceeds chip slice width");
        if (count != 0)
            ++result.affectedChips;
        result.erroneousBits += count;
    }

    if (result.affectedChips == 0)
        result.outcome = AbstractChipkillOutcome::CLEAN;
    else if (result.affectedChips == 1)
        result.outcome = AbstractChipkillOutcome::SINGLE_CHIP_CORRECTABLE;
    else
        result.outcome = AbstractChipkillOutcome::OUTSIDE_MODEL_CAPABILITY;
    return result;
}
} // namespace reliable_pim
} // namespace gem5
