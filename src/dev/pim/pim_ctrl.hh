#ifndef __DEV_PIM_PIM_CTRL_HH__
#define __DEV_PIM_PIM_CTRL_HH__

#include <cstdint>

#include "dev/io_device.hh"
#include "params/PIMCtrl.hh"
#include "sim/eventq.hh"
#include "mem/pim_op.hh"



namespace gem5
{

namespace memory {
class PIMSim;
}

class PIMCtrl : public BasicPioDevice
{
  private:
    /*
     * MMIO 区域大小：
     * PIMCtrl 占用 0x1000 字节地址空间。
     *
     * 例如：
     *   物理地址 0x2F000000 ~ 0x2F000FFF
     */
    static constexpr Addr PIM_REG_SIZE = 0x1000;

    /*
     * PIM MMIO 寄存器偏移。
     * 测试程序通过写这些寄存器触发 PIM 操作。
     */
    enum RegOffset : Addr
    {
        REG_CMD      = 0x00,  // PIM 操作类型：GEMV / ADD / MUL / RELU
        REG_STATUS   = 0x08,  // 状态寄存器：busy / done / error
        REG_SRC0     = 0x10,  // 输入 0，第一版建议表示 PIM row
        REG_SRC1     = 0x18,  // 输入 1，第一版建议表示 PIM row
        REG_DST      = 0x20,  // 输出 row
        REG_NUM_TILE = 0x28,  // tile 数量
        REG_M        = 0x30,  // GEMV 的 M 维度
        REG_N        = 0x38,  // GEMV 的 N 维度
        REG_K        = 0x40,  // 预留，后续 GEMM 使用
        REG_START    = 0x48,  // 写 1 启动 PIM 操作
        REG_CLEAR    = 0x50,  // 写 1 清除状态
        REG_ERR      = 0x58   // 错误码
    };

    /*
     * CMD 编号。
     */
    enum CmdCode : uint64_t
    {
        CMD_NOP  = 0,
        CMD_GEMV = 1,
        CMD_ADD  = 2,
        CMD_MUL  = 3,
        CMD_RELU = 4
    };

    /*
     * STATUS 位定义。
     */
    enum StatusBits : uint64_t
    {
        STATUS_BUSY  = 1ULL << 0,
        STATUS_DONE  = 1ULL << 1,
        STATUS_ERROR = 1ULL << 2
    };

    /*
     * ERR 错误码。
     */
    enum ErrorCode : uint64_t
    {
        ERR_NONE        = 0,
        ERR_BUSY        = 1,
        ERR_BAD_CMD     = 2,
        ERR_BAD_PARAM   = 3,
        ERR_NO_PIM_MEM  = 4
    };

  private:
    /*
     * 指向 PIMSim 内存对象。
     * PIMCtrl 只负责接收 MMIO 请求，真正的 PIM 操作交给 PIMSim。
     */
    memory::PIMSim *pimMem;

    /*
     * MMIO 寄存器保存值。
     */
    uint64_t cmd;
    uint64_t status;
    uint64_t err;

    uint64_t src0;
    uint64_t src1;
    uint64_t dst;
    uint64_t numTile;

    uint32_t m;
    uint32_t n;
    uint32_t k;

    /*
     * PIM 操作完成事件。
     * startPIM() 启动操作后，按照 PIMSim 返回的 latency 调度 doneEvent。
     */
    EventFunctionWrapper doneEvent;

    void startPIM();
    void finishPIM();
    void clearStatus();

    uint64_t readReg(Addr offset) const;
    void writeReg(Addr offset, uint64_t value);

  public:
    PIMCtrl(const PIMCtrlParams &p);

    Tick read(PacketPtr pkt) override;
    Tick write(PacketPtr pkt) override;
};

} // namespace gem5

#endif