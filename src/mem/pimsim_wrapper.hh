#ifndef __MEM_PIMSIM_WRAPPER_HH__
#define __MEM_PIMSIM_WRAPPER_HH__

#include <cstdint>
#include <functional>
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
  // 一次实际Bank访问的信息。
  struct PIMBankAccess
  {
      // 触发包携带的地址，仅用于调试。
      uint64_t triggerAddress;

      // 实际被访问Bank所属的位置。
      unsigned channel;
      unsigned rank;
      unsigned bank;

      // 该次Bank访问实际使用的行列。
      unsigned row;
      unsigned column;

      // 访问的数据字节数。
      unsigned bytes;

      // false表示读，true表示写。
      bool isWrite;
  };

  // 上层提供的访问接收函数。
  using PIMAccessSink =
      std::function<void(const PIMBankAccess&)>;

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
    // 只在当前PIM命令采集期间使用。
    PIMAccessSink activeAccessSink;

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
    // uint64_t executePIM(const PIMOp &op);
    uint64_t executePIM(
    const PIMOp &op,
    PIMAccessSink sink = {});

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