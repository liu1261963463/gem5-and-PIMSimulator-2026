#ifndef __MEM_RELIABLE_PIM_ODD_EVEN_PIPELINE_HH__
#define __MEM_RELIABLE_PIM_ODD_EVEN_PIPELINE_HH__
#include <cstdint>
#include <vector>
#include "mem/reliable_pim/rank_correction.hh"
namespace gem5
{
namespace reliable_pim
{
enum class PimBankParity { EVEN, ODD };
struct GroupColumnCommand
{
    PimBankParity group;
    uint32_t row;
    uint32_t column;
};
struct OddEvenTiming
{
    uint64_t tRRD;
    uint64_t tFAW;
    uint64_t tRCD;
    uint64_t tCCD;
    uint64_t pimDuration;
};
enum class PipelineEventKind { ACT, PIM };
struct PipelineEvent
{
    PipelineEventKind kind;
    uint64_t cycle;
    uint64_t endCycle; // PIM execution interval; ACT endCycle == cycle.
    unsigned bank; // Meaningful only for ACT.
    std::size_t commandIndex; // Meaningful only for PIM.
    PimBankParity group;
    uint32_t row;
    uint32_t column;
};
struct OddEvenSchedule
{
    std::vector<PipelineEvent> events;
    uint64_t finishCycle = 0;
    uint64_t pimBusyCycles = 0;
};

// Standalone cycle model, not connected to DRAMSim command queues.
// One rank, initially closed rows, one row per parity group, no refresh/PRE.
// Single command slot per cycle; single shared PIM execution resource.
// Input column-command order is preserved (no CRF/GRF dependency inference).
class OddEvenPipeline
{
  public:
    OddEvenPipeline(uint32_t channel, uint32_t rank, unsigned banks,
                    OddEvenTiming timing);
    // false: wait for all involved banks to meet tRCD before first column command.
    // true: current group may execute while the other is being activated.
    OddEvenSchedule schedule(const std::vector<GroupColumnCommand> &commands,
                             bool enablePipeline) const;
    std::vector<RankAccessTarget> correctionTargets(
        const GroupColumnCommand &triggeringCommand) const;
  private:
    uint32_t channelId, rankId;
    unsigned bankCount;
    OddEvenTiming timing;
};
} // namespace reliable_pim
} // namespace gem5
#endif
