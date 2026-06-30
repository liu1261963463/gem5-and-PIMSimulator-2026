#ifndef __MEM_PIMSIM_HH__
#define __MEM_PIMSIM_HH__


#include <deque>
#include <memory>
#include <string>
#include <unordered_map>

#include "mem/pim_op.hh"

#include "mem/abstract_mem.hh"
#include "mem/packet.hh"
#include "mem/pimsim_wrapper.hh"
#include "mem/qport.hh"
#include "params/PIMSim.hh"
#include "sim/eventq.hh"
#include "mem/pim_cmd.hh"

namespace gem5
{

namespace memory
{

class PIMSim : public AbstractMemory
{
  private:
    class MemoryPort : public ResponsePort
    {
      private:
        PIMSim &mem;

      public:
        MemoryPort(const std::string &name, PIMSim &memory);

      protected:
        Tick recvAtomic(PacketPtr pkt) override;
        void recvFunctional(PacketPtr pkt) override;
        bool recvTimingReq(PacketPtr pkt) override;
        void recvRespRetry() override;
        AddrRangeList getAddrRanges() const override;
    };

    MemoryPort port;

    PIMSimWrapper wrapper;
    Tick pimCycle;

    /*
     * Packets waiting for PIMSimulator read/write completion.
     * Use deque because multiple requests may target the same address.
     */
    std::unordered_map<Addr, std::deque<PacketPtr>> outstandingReads;
    std::unordered_map<Addr, std::deque<PacketPtr>> outstandingWrites;

    /*
     * Responses waiting to be sent back to gem5 interconnect/cache.
     */
    std::deque<PacketPtr> responseQueue;

    /*
     * Upstream request port retry state.
     */
    bool retryReq;

    /*
     * Downstream response retry state.
     */
    bool retryResp;

    bool pimOpInProgress;

    /*
     * Optional counters for debug/statistics of ignored PIM-internal callbacks.
     */
    uint64_t ignoredPimReads;
    uint64_t ignoredPimWrites;


    

    /*
     * gem5 tick when PIMSim starts ticking. Used only for debug comparison
     * against PIMSimulator's internal cycle count.
     */
    Tick startTick;

    /*
     * gem5 may require this packet to remain valid until recvTimingReq()
     * returns. This follows gem5's DRAMSim2-style memory object behavior.
     */
    std::unique_ptr<Packet> pendingDelete;

    /*
     * Event used to send timing responses.
     */
    EventFunctionWrapper sendResponseEvent;

    /*
     * Event used to advance PIMSimulator by one memory cycle.
     */
    EventFunctionWrapper tickEvent;

    /*
     * Return the number of outstanding read/write/response packets.
     */
    unsigned int nbrOutstanding() const;

    /*
     * Perform functional memory access and queue response if needed.
     */
    void accessAndRespond(PacketPtr pkt);

    /*
     * Try to send the oldest response packet.
     */
    void sendResponse();

    /*
     * Advance PIMSimulator by one cycle.
     */
    void tick();

  public:
    PARAMS(PIMSim);
    PIMSim(const Params &p);
     Tick startPimCommand(const PimCommand &cmd);

    Port &getPort(const std::string &if_name,
                  PortID idx = InvalidPortID) override;

    void init() override;
    void startup() override;
    DrainState drain() override;

     /*
     * 新增：供 PIMCtrl 通过 MMIO 调用。
     * 返回本次 PIM 操作在 gem5 中应等待的 Tick 数。
     */
    Tick submitPIMOp(const PIMOp &op);
    /*
     * PIMSimulator read/write completion callbacks.
     */
    void readComplete(unsigned id, uint64_t addr, uint64_t cycle);
    void writeComplete(unsigned id, uint64_t addr, uint64_t cycle);

  protected:
    Tick recvAtomic(PacketPtr pkt);
    void recvFunctional(PacketPtr pkt);
    bool recvTimingReq(PacketPtr pkt);
    void recvRespRetry();
};

} // namespace memory
} // namespace gem5

#endif // __MEM_PIMSIM_HH__