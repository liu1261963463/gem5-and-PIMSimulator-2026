#include <algorithm>
#include <array>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include "mem/reliable_pim/odd_even_pipeline.hh"
using namespace gem5::reliable_pim;
static void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class F> static void rejects(F f)
{
    bool caught = false;
    try { f(); } catch (const std::invalid_argument &) { caught = true; }
    require(caught, "unsupported input rejected");
}
static unsigned validate(const OddEvenSchedule &s,
    const std::vector<GroupColumnCommand> &cmds, unsigned banks, OddEvenTiming t)
{
    std::vector<uint64_t> acts;
    std::vector<bool> active(banks, false);
    std::vector<uint64_t> ready(banks, 0);
    std::vector<uint32_t> rows(banks, 0);
    std::size_t next = 0;
    uint64_t busyUntil = 0, lastColumn = 0, previousCycle = 0;
    bool haveEvent = false;
    unsigned overlaps = 0;
    for (const auto &e : s.events) {
        require(!haveEvent || e.cycle > previousCycle, "one command per cycle");
        haveEvent = true; previousCycle = e.cycle;
        if (e.kind == PipelineEventKind::ACT) {
            require(e.bank < banks && !active[e.bank], "one ACT per closed row");
            require(acts.empty() || e.cycle - acts.back() >= t.tRRD, "global tRRD");
            if (acts.size() >= 4)
                require(e.cycle - acts[acts.size()-4] >= t.tFAW, "global four-ACT window");
            acts.push_back(e.cycle);
            active[e.bank] = true; ready[e.bank] = e.cycle + t.tRCD; rows[e.bank] = e.row;
            for (const auto &p : s.events)
                if (p.kind == PipelineEventKind::PIM && p.group != e.group &&
                    p.cycle <= e.cycle && e.cycle < p.endCycle) { ++overlaps; break; }
        } else {
            require(next < cmds.size() && e.commandIndex == next, "column dependency order");
            const auto &cmd = cmds[next];
            require(e.group == cmd.group && e.row == cmd.row && e.column == cmd.column,
                    "exact originating command coordinates");
            require(e.cycle >= busyUntil, "shared PIM resource");
            require(next == 0 || e.cycle - lastColumn >= t.tCCD, "column spacing");
            const unsigned g = cmd.group == PimBankParity::EVEN ? 0 : 1;
            for (unsigned b = g; b < banks; b += 2)
                require(active[b] && e.cycle >= ready[b] && rows[b] == cmd.row,
                        "all group banks meet tRCD");
            require(e.endCycle - e.cycle == t.pimDuration, "unchanged compute duration");
            busyUntil = e.endCycle; lastColumn = e.cycle; ++next;
        }
    }
    require(next == cmds.size() && s.finishCycle == busyUntil, "all commands completed");
    require(s.pimBusyCycles == cmds.size() * t.pimDuration, "busy accounting");
    return overlaps;
}
int main(int argc, char **argv)
{
    const OddEvenTiming timing{2,20,4,2,8}; // Synthetic cycles, NOT calibrated HBM.
    OddEvenPipeline model(0,0,8,timing);
    std::vector<GroupColumnCommand> cmds;
    for (unsigned col = 0; col < 4; ++col) cmds.push_back({PimBankParity::EVEN,10,col});
    for (unsigned col = 0; col < 4; ++col) cmds.push_back({PimBankParity::ODD,10,col});
    const auto serial = model.schedule(cmds,false);
    const auto piped = model.schedule(cmds,true);
    require(validate(serial,cmds,8,timing) == 0, "baseline no overlap");
    uint64_t allRowsReady = 0;
    for (const auto &e : serial.events)
        if (e.kind == PipelineEventKind::ACT)
            allRowsReady = std::max(allRowsReady, e.cycle + timing.tRCD);
    for (const auto &e : serial.events)
        if (e.kind == PipelineEventKind::PIM) {
            require(e.cycle >= allRowsReady, "baseline waits for every bank tRCD");
            break;
        }

    const auto overlaps = validate(piped,cmds,8,timing);
    require(overlaps > 0 && piped.finishCycle < serial.finishCycle, "actual modeled overlap");
    std::cout << "PASS: command order, tRRD/tFAW/tRCD/tCCD and shared resources\n";
    std::cout << "SYNTHETIC: baseline=" << serial.finishCycle << " pipeline=" << piped.finishCycle
              << " cycles, opposite-group ACT overlaps=" << overlaps << '\n';
    for (std::size_t index : {std::size_t(0),std::size_t(4)}) {
        const auto targets = model.correctionTargets(cmds[index]);
        require(targets.size() == 4, "half-bank recovery scope");
        for (const auto &a : targets)
            require(a.channel == 0 && a.rank == 0 && a.row == 10 &&
                    a.column == cmds[index].column &&
                    a.bank % 2 == (index == 0 ? 0 : 1), "triggering command scope");
    }
    RankCorrectionEngine fsm;
    fsm.start(1,model.correctionTargets(cmds[4]),0);
    require(fsm.currentBank() == 1 && fsm.context().targets.size() == 4,
            "FSM consumes odd-only target list");
    std::cout << "PASS: exact triggering column and half-bank FSM targets\n";
    require(model.schedule({},true).finishCycle == 0, "empty schedule");
    auto crossing = cmds; crossing.push_back({PimBankParity::EVEN,11,0});
    rejects([&] { model.schedule(crossing,true); });
    rejects([&] { OddEvenPipeline bad(0,0,7,timing); });
    rejects([&] { OddEvenPipeline bad(0,0,8,{0,20,4,2,8}); });
    // An alternating stream with different rows between groups remains legal.
    const std::vector<GroupColumnCommand> alternating{
        {PimBankParity::EVEN,10,0},{PimBankParity::ODD,12,0},
        {PimBankParity::EVEN,10,1},{PimBankParity::ODD,12,1}};
    validate(model.schedule(alternating,true),alternating,8,timing);
    std::cout << "PASS: input validation and alternating groups\n";
    if (argc == 2 && std::string(argv[1]) == "--trace") {
        for (const auto &e : piped.events) {
            if (e.kind == PipelineEventKind::ACT)
                std::cout << "ACT cycle=" << e.cycle << " bank=" << e.bank << " row=" << e.row << '\n';
            else
                std::cout << "PIM cycle=" << e.cycle << " end=" << e.endCycle
                          << " command=" << e.commandIndex << " group="
                          << (e.group == PimBankParity::EVEN ? "EVEN" : "ODD")
                          << " row=" << e.row << " column=" << e.column << '\n';
        }
    }
    std::cout << "NOTE: standalone timing model; not gem5/DRAMSim performance\n";
}
