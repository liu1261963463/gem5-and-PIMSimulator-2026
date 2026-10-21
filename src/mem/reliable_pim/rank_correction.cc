#include "mem/reliable_pim/rank_correction.hh"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace gem5
{
namespace reliable_pim
{
void RankCorrectionEngine::enter(CorrectionState state)
{
    if (serial == std::numeric_limits<uint64_t>::max())
        throw std::overflow_error("Rank FSM operation token exhausted");
    ctx.operationToken = ++serial;
    ctx.state = state;
}
void RankCorrectionEngine::expect(CorrectionState expected, uint64_t t) const
{
    if (ctx.state != expected || t != ctx.operationToken)
        throw std::logic_error("Rank FSM unexpected state or stale completion token");
}
void RankCorrectionEngine::start(uint64_t txnId,
    const std::vector<RankAccessTarget> &targets, Tick now)
{
    if (ctx.state != CorrectionState::IDLE)
        throw std::logic_error("Rank FSM start requires IDLE; reset terminal state first");
    if (targets.empty())
        throw std::invalid_argument("Rank FSM requires actual command access targets");
    for (std::size_t i = 0; i < targets.size(); ++i) {
        if (targets[i].bank >= 32)
            throw std::invalid_argument("Rank FSM bank exceeds current 32-bank model");
        if (std::find(targets.begin(), targets.begin() + i, targets[i]) !=
            targets.begin() + i)
            throw std::invalid_argument("Rank FSM duplicate target");
    }
    ctx = CorrectionContext{};
    ctx.txnId = txnId;
    ctx.startTick = now;
    ctx.targets = targets;
    enter(CorrectionState::SCAN_BANK);
}
const RankAccessTarget &RankCorrectionEngine::currentTarget() const
{
    if (ctx.state != CorrectionState::SCAN_BANK &&
        ctx.state != CorrectionState::RANK_DECODE &&
        ctx.state != CorrectionState::WRITEBACK)
        throw std::logic_error("Rank FSM has no active bank access in this state");
    return ctx.targets.at(ctx.targetIndex);
}
void RankCorrectionEngine::fail(CorrectionFailure reason)
{
    ctx.failure = reason;
    enter(CorrectionState::FAILED);
}
void RankCorrectionEngine::readComplete(uint64_t t, bool success)
{
    expect(CorrectionState::SCAN_BANK, t);
    if (!success) { fail(CorrectionFailure::READ_FAILED); return; }
    enter(CorrectionState::RANK_DECODE);
}
void RankCorrectionEngine::decodeComplete(uint64_t t, RankDecodeStatus status)
{
    expect(CorrectionState::RANK_DECODE, t);
    if (status != RankDecodeStatus::CLEAN && status != RankDecodeStatus::CORRECTED &&
        status != RankDecodeStatus::UNCORRECTABLE)
        throw std::invalid_argument("Rank FSM invalid decode status");
    ++ctx.decodedTargets;
    if (status == RankDecodeStatus::UNCORRECTABLE) {
        fail(CorrectionFailure::UNCORRECTABLE); return;
    }
    if (status == RankDecodeStatus::CORRECTED)
        ++ctx.correctedTargets;
    // Backend must retain checked/corrected data and valid ECC for writeback.
    enter(CorrectionState::WRITEBACK);
}
void RankCorrectionEngine::writebackComplete(uint64_t t, bool success)
{
    expect(CorrectionState::WRITEBACK, t);
    if (!success) { fail(CorrectionFailure::WRITEBACK_FAILED); return; }
    ++ctx.writtenTargets;
    ++ctx.targetIndex;
    enter(ctx.targetIndex == ctx.targets.size()
          ? CorrectionState::READY_RETRY : CorrectionState::SCAN_BANK);
}
void RankCorrectionEngine::retryIssued(uint64_t t)
{
    expect(CorrectionState::READY_RETRY, t);
    enter(CorrectionState::RETRY);
}
void RankCorrectionEngine::retryComplete(uint64_t t, bool success)
{
    expect(CorrectionState::RETRY, t);
    if (!success) { fail(CorrectionFailure::RETRY_FAILED); return; }
    enter(CorrectionState::COMPLETE);
}
void RankCorrectionEngine::reset()
{
    if (ctx.state != CorrectionState::IDLE && ctx.state != CorrectionState::COMPLETE &&
        ctx.state != CorrectionState::FAILED)
        throw std::logic_error("Rank FSM cannot reset an in-flight recovery");
    ctx = CorrectionContext{};
}
} // namespace reliable_pim
} // namespace gem5
