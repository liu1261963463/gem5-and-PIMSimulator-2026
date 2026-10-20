#ifndef __MEM_RELIABLE_PIM_RANK_CORRECTION_HH__
#define __MEM_RELIABLE_PIM_RANK_CORRECTION_HH__

#include <cstdint>
#include <vector>

#include "base/types.hh"
#include "mem/reliable_pim/reliable_pim_types.hh"

namespace gem5
{
namespace reliable_pim
{

enum class CorrectionState
{
    IDLE,

    SCAN_BANK,
    RANK_DECODE,

    WRITEBACK,

    CODEWORD_FLIP,

    RETRY,

    COMPLETE,

    FAILED
};


struct CorrectionContext
{
    uint64_t txnId = 0;

    CorrectionState state =
        CorrectionState::IDLE;

    std::vector<unsigned> banks;

    unsigned bankIndex = 0;

    unsigned faultyChip = 0;

    Tick startTick = 0;
};


class RankCorrectionEngine
{
  public:

    RankCorrectionEngine();

    void
    start(
        uint64_t txnId,
        const std::bitset<32> &activeBanks,
        Tick now);

    CorrectionState
    state() const;

    unsigned
    currentBank() const;

    void
    advance();

    void
    reset();

    const CorrectionContext&
    context() const
    {
        return ctx;
    }

  private:

    CorrectionContext ctx;
};

} // namespace reliable_pim
} // namespace gem5

#endif