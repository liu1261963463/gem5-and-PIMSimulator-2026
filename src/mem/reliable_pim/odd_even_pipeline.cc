#include "mem/reliable_pim/odd_even_pipeline.hh"
#include <array>
#include <deque>
#include <limits>
#include <stdexcept>
namespace gem5
{
namespace reliable_pim
{
namespace
{
unsigned parity(PimBankParity g)
{
    if (g == PimBankParity::EVEN) return 0;
    if (g == PimBankParity::ODD) return 1;
    throw std::invalid_argument("invalid bank parity");
}
uint64_t add(uint64_t a, uint64_t b)
{
    if (b > std::numeric_limits<uint64_t>::max() - a)
        throw std::overflow_error("pipeline cycle overflow");
    return a + b;
}
}
OddEvenPipeline::OddEvenPipeline(uint32_t channel, uint32_t rank,
    unsigned banks, OddEvenTiming t)
    : channelId(channel), rankId(rank), bankCount(banks), timing(t)
{
    if (banks < 2 || banks > 32 || banks % 2 != 0)
        throw std::invalid_argument("pipeline requires an even bank count in [2,32]");
    if (!t.tRRD || !t.tFAW || !t.tRCD || !t.tCCD || !t.pimDuration)
        throw std::invalid_argument("all abstract pipeline timing values must be positive");
}
std::vector<RankAccessTarget> OddEvenPipeline::correctionTargets(
    const GroupColumnCommand &command) const
{
    std::vector<RankAccessTarget> targets;
    for (unsigned bank = parity(command.group); bank < bankCount; bank += 2)
        targets.push_back({channelId, rankId, bank, command.row, command.column});
    return targets;
}
OddEvenSchedule OddEvenPipeline::schedule(
    const std::vector<GroupColumnCommand> &commands, bool enablePipeline) const
{
    OddEvenSchedule out;
    if (commands.empty()) return out;
    std::array<bool,2> used{};
    std::array<uint32_t,2> rows{};
    for (const auto &cmd : commands) {
        const unsigned g = parity(cmd.group);
        if (used[g] && rows[g] != cmd.row)
            throw std::invalid_argument("row change needs PRE/tRAS/tRP: unsupported in this model");
        used[g] = true; rows[g] = cmd.row;
    }
    std::vector<bool> active(bankCount, false);
    std::vector<uint64_t> ready(bankCount, 0);
    std::deque<uint64_t> actWindow;
    uint64_t now = 0, nextAct = 0, nextColumn = 0, pimFree = 0;
    std::size_t index = 0;
    while (index < commands.size()) {
        while (!actWindow.empty() && now - actWindow.front() >= timing.tFAW)
            actWindow.pop_front();
        const auto &cmd = commands[index];
        const unsigned group = parity(cmd.group);
        bool groupReady = true, allReady = true;
        for (unsigned bank = 0; bank < bankCount; ++bank) {
            if (bank % 2 == group && (!active[bank] || ready[bank] > now))
                groupReady = false;
            if (used[bank % 2] && (!active[bank] || ready[bank] > now))
                allReady = false;
        }
        // Prefer a ready column operation, then fill command slots with ACTs.
        if (groupReady && (enablePipeline || allReady) &&
            now >= nextColumn && now >= pimFree) {
            const uint64_t end = add(now, timing.pimDuration);
            out.events.push_back({PipelineEventKind::PIM, now, end, 0, index,
                                  cmd.group, cmd.row, cmd.column});
            out.pimBusyCycles = add(out.pimBusyCycles, timing.pimDuration);
            pimFree = end;
            nextColumn = add(now, timing.tCCD);
            ++index;
        } else if (now >= nextAct && actWindow.size() < 4) {
            unsigned selected = bankCount;
            for (unsigned pass = 0; pass < 2 && selected == bankCount; ++pass) {
                const unsigned g = pass == 0 ? group : 1 - group;
                if (!used[g]) continue;
                for (unsigned bank = g; bank < bankCount; bank += 2)
                    if (!active[bank]) { selected = bank; break; }
            }
            if (selected != bankCount) {
                const auto g = selected % 2 == 0 ? PimBankParity::EVEN : PimBankParity::ODD;
                out.events.push_back({PipelineEventKind::ACT, now, now, selected, 0,
                                      g, rows[selected % 2], 0});
                active[selected] = true;
                ready[selected] = add(now, timing.tRCD);
                nextAct = add(now, timing.tRRD);
                actWindow.push_back(now);
            }
        }
        now = add(now, 1);
    }
    out.finishCycle = pimFree;
    return out;
}
} // namespace reliable_pim
} // namespace gem5
