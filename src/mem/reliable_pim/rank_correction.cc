#include "mem/reliable_pim/rank_correction.hh"

namespace gem5
{
namespace reliable_pim
{

RankCorrectionEngine::RankCorrectionEngine()
{
}


void
RankCorrectionEngine::start(
    uint64_t txnId,
    const std::bitset<32> &activeBanks,
    Tick now)
{
    ctx = CorrectionContext{};

    ctx.txnId = txnId;
    ctx.startTick = now;

    for (unsigned bank = 0;
         bank < 32;
         ++bank)
    {
        if (activeBanks.test(bank))
            ctx.banks.push_back(bank);
    }

    ctx.bankIndex = 0;

    ctx.state =
        CorrectionState::SCAN_BANK;
}


CorrectionState
RankCorrectionEngine::state() const
{
    return ctx.state;
}


unsigned
RankCorrectionEngine::currentBank() const
{
    if (ctx.bankIndex >=
        ctx.banks.size())
    {
        return 0;
    }

    return ctx.banks[ctx.bankIndex];
}


void
RankCorrectionEngine::advance()
{
    switch (ctx.state)
    {
      case CorrectionState::SCAN_BANK:

        ctx.bankIndex++;

        if (ctx.bankIndex >=
            ctx.banks.size())
        {
            ctx.state =
                CorrectionState::RANK_DECODE;
        }

        break;

      case CorrectionState::RANK_DECODE:

        ctx.state =
            CorrectionState::WRITEBACK;

        break;

      case CorrectionState::WRITEBACK:

        ctx.state =
            CorrectionState::CODEWORD_FLIP;

        break;

      case CorrectionState::CODEWORD_FLIP:

        ctx.state =
            CorrectionState::RETRY;

        break;

      case CorrectionState::RETRY:

        ctx.state =
            CorrectionState::COMPLETE;

        break;

      default:
        break;
    }
}


void
RankCorrectionEngine::reset()
{
    ctx = CorrectionContext{};
}

} // namespace reliable_pim
} // namespace gem5