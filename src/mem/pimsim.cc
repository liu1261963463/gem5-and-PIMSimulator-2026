/*
 * PIMSim memory object for gem5.
 *
 * This file connects gem5's memory request path to PIMSimulator through
 * PIMSimWrapper. It is intended to be placed at:
 *
 *   gem5/src/mem/pimsim.cc
 *
 * Required companion files:
 *   gem5/src/mem/pimsim.hh
 *   gem5/src/mem/pimsim_wrapper.hh
 *   gem5/src/mem/pimsim_wrapper.cc
 *   gem5/src/mem/PIMSim.py
 */

#include "mem/pimsim.hh"

#include "mem/pim_op.hh"

#include "mem/pimsim_half_compat.hh"

#include "Callback.h"

#include "base/callback.hh"
#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/Drain.hh"
#include "debug/PIMSim.hh"
#include "sim/system.hh"

namespace gem5
{

namespace memory
{
PIMSim::PIMSim(const Params &p)
    : AbstractMemory(p),
      port(name() + ".port", *this),
      wrapper(p.deviceConfigFile,
              p.systemConfigFile,
              p.filePath,
              p.traceFile,
              p.range.size() / 1024 / 1024,
              p.enableDebug,
              p.pimChannels,
              p.pimRanks),
      pimCycle(p.pimCycle),
      retryReq(false),
      retryResp(false),
      pimOpInProgress(false),
      ignoredPimReads(0),
      ignoredPimWrites(0),
      startTick(0),
      sendResponseEvent([this] { sendResponse(); }, name()),
      tickEvent([this] { tick(); }, name())
{
    DPRINTF(PIMSim,
            "Instantiated PIMSim: clock=%f ns, queue_size=%u, "
            "burst_size=%u bytes\n",
            wrapper.clockPeriod(), wrapper.queueSize(), wrapper.burstSize());

    /*
     * PIMSimulator inherits the DRAMSim2-style callback interface.
     * The callback signature is:
     *
     *   void callback(unsigned id, uint64_t addr, uint64_t cycle)
     */
    DRAMSim::TransactionCompleteCB *read_cb =
        new DRAMSim::Callback<PIMSim, void, unsigned, uint64_t, uint64_t>(
            this, &PIMSim::readComplete);

    DRAMSim::TransactionCompleteCB *write_cb =
        new DRAMSim::Callback<PIMSim, void, unsigned, uint64_t, uint64_t>(
            this, &PIMSim::writeComplete);

    wrapper.setCallbacks(read_cb, write_cb);

    /*
     * gem5 may not call the C++ destructor at simulation exit. Register
     * an exit callback to dump PIMSimulator-side statistics.
     */
    registerExitCallback([this]() { wrapper.printStats(); });
}

Tick
PIMSim::startPimCommand(const PimCommand &cmd)
{
    const uint64_t pim_cycles = wrapper.executePimCommand(cmd);

    DPRINTF(PIMSim,
            "PIM command finished: opcode=%lu pim_cycles=%lu "
            "gem5_latency=%lu\n",
            static_cast<uint64_t>(cmd.opcode),
            pim_cycles,
            pim_cycles * pimCycle);

    return pim_cycles * pimCycle;
}

Tick
PIMSim::submitPIMOp(const PIMOp &op)
{
    /*
     * PIMCtrl/MMIO-triggered PIM kernels issue PIMSimulator-internal memory
     * transactions. Their completion callbacks are not responses to gem5 CPU
     * packets, so readComplete()/writeComplete() must not look for a PacketPtr
     * in outstandingReads/outstandingWrites while this call is active.
     */
    pimOpInProgress = true;
    const uint64_t pim_cycles = wrapper.executePIM(op);
    pimOpInProgress = false;

    /*
     * If PIMSimulator does not report a valid cycle count, use one cycle to
     * avoid completing the MMIO operation at zero latency.
     */
    const uint64_t cycles = pim_cycles == 0 ? 1 : pim_cycles;

    return cyclesToTicks(Cycles(cycles));
}


void
PIMSim::init()
{
    AbstractMemory::init();

    if (!port.isConnected()) {
        fatal("PIMSim %s is unconnected!\n", name());
    }

    port.sendRangeChange();

    /*
     * PIMSimulator HBM2 configurations often use a 32-byte burst.
     * gem5 often uses a 64-byte cache line by default. This first-stage
     * integration assumes one gem5 packet is mapped to one PIMSimulator
     * transaction. For accurate timing, either:
     *
     *   1. run gem5 with --cacheline_size equal to wrapper.burstSize(), or
     *   2. extend recvTimingReq() to split one gem5 packet into multiple
     *      PIMSimulator transactions.
     */
    if (system()->cacheLineSize() != wrapper.burstSize()) {
        warn("PIMSim burst size %u bytes does not match gem5 cache line "
             "size %u bytes. This first-stage version assumes one gem5 "
             "packet maps to one PIMSimulator transaction. Consider using "
             "--cacheline_size=%u or implement transaction splitting.\n",
             wrapper.burstSize(),
             system()->cacheLineSize(),
             wrapper.burstSize());
    }
}

void
PIMSim::startup()
{
    startTick = curTick();

    if (!tickEvent.scheduled()) {
        schedule(tickEvent, clockEdge());
    }
}

unsigned int
PIMSim::nbrOutstanding() const
{
    unsigned int total = responseQueue.size();

    for (const auto &entry : outstandingReads) {
        total += entry.second.size();
    }

    for (const auto &entry : outstandingWrites) {
        total += entry.second.size();
    }

    return total;
}

void
PIMSim::sendResponse()
{
    assert(!retryResp);
    assert(!responseQueue.empty());

    DPRINTF(PIMSim, "Attempting to send response, queue_size=%zu\n",
            responseQueue.size());

    PacketPtr pkt = responseQueue.front();
    const bool success = port.sendTimingResp(pkt);

    if (success) {
        responseQueue.pop_front();

        DPRINTF(PIMSim,
                "Response sent. outstanding=%u, response_queue=%zu\n",
                nbrOutstanding(), responseQueue.size());

        if (!responseQueue.empty() && !sendResponseEvent.scheduled()) {
            schedule(sendResponseEvent, curTick());
        }

        if (nbrOutstanding() == 0) {
            signalDrainDone();
        }
    } else {
        retryResp = true;

        DPRINTF(PIMSim, "Response blocked. Waiting for recvRespRetry().\n");

        assert(!sendResponseEvent.scheduled());
    }
}

void
PIMSim::tick()
{
    wrapper.tick();

    /*
     * If a request was previously rejected because PIMSimulator's
     * transaction queue was full, check whether progress has been made.
     */
    if (retryReq &&
        nbrOutstanding() < wrapper.queueSize() &&
        wrapper.canAccept()) {
        retryReq = false;

        DPRINTF(PIMSim, "Sending retry request to upstream port.\n");

        port.sendRetryReq();
    }

    const Tick next_tick =
        curTick() +
        static_cast<Tick>(wrapper.clockPeriod() * sim_clock::as_int::ns);

    schedule(tickEvent, next_tick);
}

Tick
PIMSim::recvAtomic(PacketPtr pkt)
{
    /*
     * Atomic mode is used for fast functional-style simulation. We do not
     * call PIMSimulator here because PIMSimulator is timing-oriented.
     */
    access(pkt);

    if (pkt->cacheResponding()) {
        return 0;
    }

    /*
     * Approximate unloaded latency for atomic accesses.
     * This value does not affect detailed timing-mode evaluation.
     */
    return static_cast<Tick>(50 * wrapper.clockPeriod() *
                             sim_clock::as_int::ns);
}

void
PIMSim::recvFunctional(PacketPtr pkt)
{
    pkt->pushLabel(name());

    /*
     * First access backing store.
     */
    functionalAccess(pkt);

    /*
     * Also check pending responses, because a functional access should
     * observe the most up-to-date data visible in the memory object.
     */
    for (auto &resp_pkt : responseQueue) {
        pkt->trySatisfyFunctional(resp_pkt);
    }

    /*
     * Check outstanding reads.
     */
    for (auto &entry : outstandingReads) {
        auto q = entry.second;

        while (!q.empty()) {
            pkt->trySatisfyFunctional(q.front());
            q.pop_front();
        }
    }

    /*
     * Check outstanding writes.
     */
    for (auto &entry : outstandingWrites) {
        auto q = entry.second;

        while (!q.empty()) {
            pkt->trySatisfyFunctional(q.front());
            q.pop_front();
        }
    }

    pkt->popLabel();
}

bool
PIMSim::recvTimingReq(PacketPtr pkt)
{
    DPRINTF(PIMSim,
            "recvTimingReq: cmd=%s addr=%#llx size=%u needsResp=%d\n",
            pkt->cmdString(), pkt->getAddr(), pkt->getSize(),
            pkt->needsResponse());

    /*
     * If another cache has already committed to responding to this packet,
     * the memory should sink it and do nothing further.
     */
    if (pkt->cacheResponding()) {
        pendingDelete.reset(pkt);
        return true;
    }

    /*
     * Once this memory object has promised a retry, it should not accept a
     * new timing request until the retry has been sent.
     */
    if (retryReq) {
        DPRINTF(PIMSim,
                "Rejecting request because retryReq is already set.\n");
        return false;
    }

    /*
     * Reject when the local outstanding count or the PIMSimulator internal
     * queue cannot accept more transactions.
     */
    const bool can_accept =
        nbrOutstanding() < wrapper.queueSize() && wrapper.canAccept();

    if (!can_accept) {
        retryReq = true;

        DPRINTF(PIMSim,
                "Rejecting request: outstanding=%u, wrapper_queue=%u, "
                "wrapper_can_accept=%d\n",
                nbrOutstanding(), wrapper.queueSize(), wrapper.canAccept());

        return false;
    }

    const Addr addr = pkt->getAddr();

    if (pkt->isRead()) {
        outstandingReads[addr].push_back(pkt);

        DPRINTF(PIMSim, "Enqueue read transaction addr=%#llx\n", addr);

        wrapper.enqueue(false, addr);

        return true;
    }

    if (pkt->isWrite()) {
        outstandingWrites[addr].push_back(pkt);

        /*
         * Follow gem5's DRAMSim2 behavior: writes are functionally applied
         * and acknowledged immediately, while the external memory simulator
         * continues tracking write timing asynchronously.
         */
        accessAndRespond(pkt);

        DPRINTF(PIMSim, "Enqueue write transaction addr=%#llx\n", addr);

        wrapper.enqueue(true, addr);

        return true;
    }

    /*
     * For non-read/write commands, keep the behavior simple and respond
     * through the backing store if required.
     */
    accessAndRespond(pkt);

    return true;
}

void
PIMSim::recvRespRetry()
{
    DPRINTF(PIMSim, "recvRespRetry received.\n");

    assert(retryResp);

    retryResp = false;

    sendResponse();
}

void
PIMSim::accessAndRespond(PacketPtr pkt)
{
    DPRINTF(PIMSim,
            "Functional access for response: cmd=%s addr=%#llx size=%u\n",
            pkt->cmdString(), pkt->getAddr(), pkt->getSize());

    const bool needs_response = pkt->needsResponse();

    /*
     * access() performs the backing-store access and turns the request into
     * a response when appropriate.
     */
    access(pkt);

    if (needs_response) {
        assert(pkt->isResponse());

        /*
         * Account for packet delays already accumulated through the xbar or
         * upstream objects, then reset them before sending the response.
         */
        const Tick response_time =
            curTick() + pkt->headerDelay + pkt->payloadDelay;

        pkt->headerDelay = 0;
        pkt->payloadDelay = 0;

        responseQueue.push_back(pkt);

        DPRINTF(PIMSim,
                "Queued response addr=%#llx response_time=%llu "
                "response_queue=%zu\n",
                pkt->getAddr(), response_time, responseQueue.size());

        if (!retryResp && !sendResponseEvent.scheduled()) {
            schedule(sendResponseEvent, response_time);
        }
    } else {
        pendingDelete.reset(pkt);
    }
}

void
PIMSim::readComplete(unsigned id, uint64_t addr, uint64_t cycle)
{
    DPRINTF(PIMSim,
            "PIMSimulator read complete: id=%u addr=%#llx cycle=%llu\n",
            id, addr, cycle);

    /*
     * PIMSimulator may maintain its own memory cycle counter. We do not
     * assert exact equality with gem5 ticks here because PIM mode changes
     * and future PIM commands may introduce extra internal cycles.
     */
    const auto expected_cycle =
        divCeil(curTick() - startTick,
                static_cast<Tick>(wrapper.clockPeriod() *
                                  sim_clock::as_int::ns));

    if (cycle != expected_cycle) {
        DPRINTF(PIMSim,
                "PIMSimulator cycle=%llu differs from gem5-derived "
                "cycle=%llu. This is usually acceptable if PIMSimulator "
                "has its own internal timing state.\n",
                cycle, expected_cycle);
    }

    auto it = outstandingReads.find(addr);

    if (it == outstandingReads.end() || it->second.empty()) {
        /*
         * Internal PIMSimulator transaction generated by PIMKernel/runPIM().
         * It does not correspond to a gem5 CPU/cache PacketPtr, so ignore it
         * instead of treating it as a missing outstanding read.
         */
        if (pimOpInProgress) {
            ++ignoredPimReads;
            DPRINTF(PIMSim,
                    "Ignore internal PIM readComplete: id=%u addr=%#llx "
                    "cycle=%llu ignored_reads=%llu\n",
                    id, addr, cycle, ignoredPimReads);
            return;
        }

        panic("PIMSim readComplete for addr=%#llx but no outstanding "
              "read packet exists.\n", addr);
    }

    /*
     * FIFO per address, same assumption as gem5's DRAMSim2 integration.
     */
    PacketPtr pkt = it->second.front();
    it->second.pop_front();

    if (it->second.empty()) {
        outstandingReads.erase(it);
    }

    accessAndRespond(pkt);
}

void
PIMSim::writeComplete(unsigned id, uint64_t addr, uint64_t cycle)
{
    DPRINTF(PIMSim,
            "PIMSimulator write complete: id=%u addr=%#llx cycle=%llu\n",
            id, addr, cycle);

    const auto expected_cycle =
        divCeil(curTick() - startTick,
                static_cast<Tick>(wrapper.clockPeriod() *
                                  sim_clock::as_int::ns));

    if (cycle != expected_cycle) {
        DPRINTF(PIMSim,
                "PIMSimulator cycle=%llu differs from gem5-derived "
                "cycle=%llu. This is usually acceptable if PIMSimulator "
                "has its own internal timing state.\n",
                cycle, expected_cycle);
    }

    auto it = outstandingWrites.find(addr);

    if (it == outstandingWrites.end() || it->second.empty()) {
        /*
         * Internal PIMSimulator transaction generated by PIMKernel/runPIM().
         * It does not correspond to a gem5 CPU/cache PacketPtr, so ignore it
         * instead of treating it as a missing outstanding write.
         */
        if (pimOpInProgress) {
            ++ignoredPimWrites;
            DPRINTF(PIMSim,
                    "Ignore internal PIM writeComplete: id=%u addr=%#llx "
                    "cycle=%llu ignored_writes=%llu\n",
                    id, addr, cycle, ignoredPimWrites);
            return;
        }

        panic("PIMSim writeComplete for addr=%#llx but no outstanding "
              "write packet exists.\n", addr);
    }

    /*
     * The write packet has already been functionally applied and responded
     * in recvTimingReq(). Here we only retire the timing transaction.
     */
    it->second.pop_front();

    if (it->second.empty()) {
        outstandingWrites.erase(it);
    }

    if (nbrOutstanding() == 0) {
        signalDrainDone();
    }
}

Port &
PIMSim::getPort(const std::string &if_name, PortID idx)
{
    if (if_name == "port") {
        return port;
    }

    return AbstractMemory::getPort(if_name, idx);
}

DrainState
PIMSim::drain()
{
    if (nbrOutstanding() != 0) {
        DPRINTF(Drain,
                "PIMSim is draining: outstanding=%u, response_queue=%zu\n",
                nbrOutstanding(), responseQueue.size());

        return DrainState::Draining;
    }

    return DrainState::Drained;
}

/*
 * MemoryPort implementation.
 */

PIMSim::MemoryPort::MemoryPort(const std::string &name, PIMSim &memory)
    : ResponsePort(name), mem(memory)
{
}

AddrRangeList
PIMSim::MemoryPort::getAddrRanges() const
{
    AddrRangeList ranges;
    ranges.push_back(mem.getAddrRange());
    return ranges;
}

Tick
PIMSim::MemoryPort::recvAtomic(PacketPtr pkt)
{
    return mem.recvAtomic(pkt);
}

void
PIMSim::MemoryPort::recvFunctional(PacketPtr pkt)
{
    mem.recvFunctional(pkt);
}

bool
PIMSim::MemoryPort::recvTimingReq(PacketPtr pkt)
{
    return mem.recvTimingReq(pkt);
}

void
PIMSim::MemoryPort::recvRespRetry()
{
    mem.recvRespRetry();
}

} // namespace memory
} // namespace gem5