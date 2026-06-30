#ifndef __MEM_PIMSIM_WRAPPER_HH__
#define __MEM_PIMSIM_WRAPPER_HH__

#include <cstdint>
#include <memory>
#include <string>

#include "mem/pim_op.hh"

#include "mem/pim_cmd.hh"

/*
 * TransactionCompleteCB is a typedef in PIMSimulator's Callback.h.
 * It must not be forward-declared as a class.
 */
#include "Callback.h"

namespace DRAMSim
{
class MultiChannelMemorySystem;
//class TransactionCompleteCB;
}

namespace gem5
{
namespace memory
{

class PIMSimWrapper
{
  private:
    std::shared_ptr<DRAMSim::MultiChannelMemorySystem> pimsim;

    unsigned pimChans;
    unsigned pimRanks;

    double _clockPeriod;
    unsigned int _queueSize;
    unsigned int _burstSize;

    void drainPIMSimulator();

  public:
    PIMSimWrapper(const std::string &device_config,
                  const std::string &system_config,
                  const std::string &file_path,
                  const std::string &trace_file,
                  unsigned int megs_of_memory,
                  bool enable_debug,
                  unsigned pim_chans,
                  unsigned pim_ranks);

    ~PIMSimWrapper();

    void setCallbacks(DRAMSim::TransactionCompleteCB *read_cb,
                      DRAMSim::TransactionCompleteCB *write_cb);

    bool canAccept() const;

    void enqueue(bool is_write, uint64_t addr);

    void tick();

    void printStats();

    /*
     * 新增：PIMCtrl 通过 PIMSim 调用到这里。
     */
    uint64_t executePIM(const PIMOp &op);

    /**
     * 从 gem5 MMIO 控制器触发 PIMSimulator kernel。
     * 返回 PIMSimulator 内部执行的 cycle 数。
     */
    uint64_t executePimCommand(const PimCommand &cmd);

    double clockPeriod() const { return _clockPeriod; }
    unsigned int queueSize() const { return _queueSize; }
    unsigned int burstSize() const { return _burstSize; }
};

}
}

#endif