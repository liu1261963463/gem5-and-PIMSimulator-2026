#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include "mem/reliable_pim/rank_correction.hh"
#include "mem/reliable_pim/pim_alert.hh"
using namespace gem5::reliable_pim;
static void require(bool ok, const char *message)
{
    if (!ok) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
template<class E, class F> static void rejects(F f, const char *message)
{
    bool caught = false;
    try { f(); } catch (const E &) { caught = true; }
    require(caught, message);
}
static void ready(RankCorrectionEngine &fsm)
{
    // Scripted backend responses: this helper performs no real ECC or I/O.
    fsm.readComplete(fsm.token(), true);
    fsm.decodeComplete(fsm.token(), RankDecodeStatus::CLEAN);
    fsm.writebackComplete(fsm.token(), true);
}
int main()
{
    const RankAccessTarget a{0, 0, 2, 10, 4};
    const RankAccessTarget b{0, 0, 5, 10, 4};
    RankCorrectionEngine fsm;
    rejects<std::invalid_argument>([&] { fsm.start(1, {}, 0); }, "empty targets");
    rejects<std::invalid_argument>([&] { fsm.start(1, {a, a}, 0); }, "duplicate targets");
    auto invalid = a; invalid.bank = 32;
    rejects<std::invalid_argument>([&] { fsm.start(1, {invalid}, 0); }, "bank bounds");
    rejects<std::logic_error>([&] { fsm.currentBank(); }, "no default bank zero");

    PimAlertLatch alert;
    alert.observe(true);
    fsm.start(7, {a, b}, 123);
    const auto oldRead = fsm.token();
    require(fsm.currentTarget() == a, "first actual target");
    rejects<std::logic_error>([&] { fsm.start(8, {a}, 0); }, "busy start");
    rejects<std::logic_error>([&] { fsm.reset(); }, "no reset while active");
    rejects<std::logic_error>([&] { fsm.writebackComplete(fsm.token(), true); }, "wrong order");
    rejects<std::logic_error>([&] { fsm.retryIssued(fsm.token()); }, "premature retry");
    fsm.readComplete(oldRead, true);
    rejects<std::logic_error>([&] { fsm.readComplete(oldRead, true); }, "duplicate read");
    fsm.decodeComplete(fsm.token(), RankDecodeStatus::CORRECTED);
    require(fsm.currentTarget() == a, "target stable before writeback ack");
    fsm.writebackComplete(fsm.token(), true);
    require(fsm.state() == CorrectionState::SCAN_BANK && fsm.currentTarget() == b,
            "next target only after writeback");
    rejects<std::logic_error>([&] { fsm.readComplete(oldRead, true); }, "old-bank callback");
    ready(fsm);
    require(fsm.state() == CorrectionState::READY_RETRY, "all targets written");
    require(fsm.context().decodedTargets == 2 && fsm.context().correctedTargets == 1 &&
            fsm.context().writtenTargets == 2, "episode accounting");
    rejects<std::logic_error>([&] { fsm.retryComplete(fsm.token(), true); }, "retry not issued");
    fsm.retryIssued(fsm.token());
    require(fsm.state() == CorrectionState::RETRY, "wait for actual retry completion");
    fsm.retryComplete(fsm.token(), true);
    require(fsm.state() == CorrectionState::COMPLETE, "successful episode");
    require(alert.pending(), "FSM cannot silently release alarm");
    rejects<std::logic_error>([&] { fsm.currentBank(); }, "terminal has no active bank");
    std::cout << "PASS: per-bank read/decode/writeback and retry completion\n";

    fsm.reset();
    fsm.start(7, {a}, 200); // Same txn id; old token must still be rejected.
    rejects<std::logic_error>([&] { fsm.readComplete(oldRead, true); }, "old-episode callback");
    fsm.readComplete(fsm.token(), false);
    require(fsm.context().failure == CorrectionFailure::READ_FAILED, "read failure");
    fsm.reset(); fsm.start(8, {a}, 0);
    fsm.readComplete(fsm.token(), true);
    fsm.decodeComplete(fsm.token(), RankDecodeStatus::UNCORRECTABLE);
    require(fsm.state() == CorrectionState::FAILED &&
            fsm.context().failure == CorrectionFailure::UNCORRECTABLE &&
            fsm.context().writtenTargets == 0, "uncorrectable is not written back");
    rejects<std::logic_error>([&] { fsm.retryIssued(fsm.token()); }, "no retry after UCE");
    fsm.reset(); fsm.start(9, {a}, 0);
    fsm.readComplete(fsm.token(), true);
    fsm.decodeComplete(fsm.token(), RankDecodeStatus::CORRECTED);
    fsm.writebackComplete(fsm.token(), false);
    require(fsm.context().failure == CorrectionFailure::WRITEBACK_FAILED &&
            fsm.context().writtenTargets == 0, "writeback failure");
    fsm.reset(); fsm.start(10, {a}, 0); ready(fsm);
    fsm.retryIssued(fsm.token());
    fsm.retryComplete(fsm.token(), false);
    require(fsm.context().failure == CorrectionFailure::RETRY_FAILED &&
            fsm.state() == CorrectionState::FAILED, "bounded retry failure");
    std::cout << "PASS: read, uncorrectable, writeback and retry failures\n";
    std::cout << "PASS: invalid order, duplicate and stale callbacks rejected\n";
    std::cout << "NOTE: scripted backend only; no real rank ECC or gem5 execution\n";
}
