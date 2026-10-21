#ifndef __MEM_RELIABLE_PIM_RANK_CORRECTION_HH__
#define __MEM_RELIABLE_PIM_RANK_CORRECTION_HH__
#include <cstddef>
#include <cstdint>
#include <vector>
#include "base/types.hh"

namespace gem5
{
namespace reliable_pim
{
enum class CorrectionState
{
    IDLE, SCAN_BANK, RANK_DECODE, WRITEBACK,
    READY_RETRY, RETRY, COMPLETE, FAILED
};
enum class RankDecodeStatus { CLEAN, CORRECTED, UNCORRECTABLE };
enum class CorrectionFailure
{
    NONE, READ_FAILED, UNCORRECTABLE, WRITEBACK_FAILED, RETRY_FAILED
};

// One full-rank access location. All chip slices belong to the backend.
// column retains the simulator's native column units; no address guessing.
struct RankAccessTarget
{
    uint32_t channel = 0;
    uint32_t rank = 0;
    uint32_t bank = 0;
    uint32_t row = 0;
    uint32_t column = 0;
    bool operator==(const RankAccessTarget &o) const
    {
        return channel == o.channel && rank == o.rank && bank == o.bank &&
               row == o.row && column == o.column;
    }
};
struct CorrectionContext
{
    uint64_t txnId = 0;
    CorrectionState state = CorrectionState::IDLE;
    CorrectionFailure failure = CorrectionFailure::NONE;
    std::vector<RankAccessTarget> targets;
    std::size_t targetIndex = 0;
    uint64_t operationToken = 0;
    uint64_t decodedTargets = 0;
    uint64_t correctedTargets = 0;
    uint64_t writtenTargets = 0;
    Tick startTick = 0;
};

// Control FSM only. Backend owns read buffers, rank ECC and physical writes.
// A state requests an action; its completion callback advances the FSM.
class RankCorrectionEngine
{
  public:
    RankCorrectionEngine() = default;
    void start(uint64_t txnId, const std::vector<RankAccessTarget> &targets,
               Tick now);
    CorrectionState state() const { return ctx.state; }
    uint64_t token() const { return ctx.operationToken; }
    const RankAccessTarget &currentTarget() const;
    unsigned currentBank() const { return currentTarget().bank; }
    const CorrectionContext &context() const { return ctx; }

    void readComplete(uint64_t token, bool success);
    void decodeComplete(uint64_t token, RankDecodeStatus status);
    void writebackComplete(uint64_t token, bool success);
    // Call only once the retry has actually been accepted for execution.
    void retryIssued(uint64_t token);
    // success means actual PIM completion with no unresolved error.
    void retryComplete(uint64_t token, bool success);
    // Only allowed while idle or terminal. Does not clear ALERT_n or data.
    void reset();

  private:
    CorrectionContext ctx;
    uint64_t serial = 0; // Never reset, so old callbacks cannot match a new run.
    void expect(CorrectionState state, uint64_t token) const;
    void enter(CorrectionState state);
    void fail(CorrectionFailure reason);
};
} // namespace reliable_pim
} // namespace gem5
#endif
