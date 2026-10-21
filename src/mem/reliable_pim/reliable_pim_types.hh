#ifndef __MEM_RELIABLE_PIM_RELIABLE_PIM_TYPES_HH__
#define __MEM_RELIABLE_PIM_RELIABLE_PIM_TYPES_HH__

#include <bitset>
#include <cstdint>

#include "base/types.hh"
#include "mem/reliable_pim/pim_alert.hh"

namespace gem5
{
namespace reliable_pim
{

// ---------------------------------------------------------
// ECC工作模式
// ---------------------------------------------------------
enum class EccMode
{
    NONE,
    CRC8,
    CRC16,
    SEC
};

// ---------------------------------------------------------
// Rank-level correction由谁控制
// ---------------------------------------------------------
enum class CorrectionMode
{
    HARDWARE,
    SOFTWARE
};

// ---------------------------------------------------------
// Dual CRC解码结果
// ---------------------------------------------------------
enum class DecodeResult
{
    NORMAL,       // 普通codeword，无异常
    FLIPPED,      // 当前存储的是flipped codeword
    ALERT         // 需要rank-level correction
};

// ---------------------------------------------------------
// PIM命令检查结果
// ---------------------------------------------------------
enum class PimCheckResult
{
    SUCCESS,
    STALL_FOR_CORRECTION,
    UNCORRECTABLE
};

// ---------------------------------------------------------
// 一个PIM访问的位置
//
// 注意：这里不要重新解码物理地址。
// 最好由现有PIMSimulator地址译码逻辑填充。
// ---------------------------------------------------------

struct PimAccessInfo
{
    // ===== 第一部分：原有物理位置信息 =====
    Addr addr = 0;
    bool addrValid = false;
    Addr triggerAddr = 0;

    uint32_t channel = 0;
    uint32_t rank = 0;
    uint32_t chip = 0;
    uint32_t bank = 0;

    uint32_t row = 0;
    uint32_t column = 0;

    // 多Bank操作
    std::bitset<32> activeBanks;

    bool isWrite = false;

    // ===== 第二部分：新增PIM命令信息 =====
    // 由PIMSim::submitPIMOp()填写
    uint32_t opcode = 0;

    Addr src0 = 0;
    Addr src1 = 0;
    Addr dst = 0;

    uint64_t numTile = 0;

    uint32_t m = 0;
    uint32_t n = 0;
    uint32_t k = 0;

    // 命令提交时间
    Tick issueTick = 0;

    // ===== 第三部分：有效性标记 =====
    // 是否已经填写PIM命令参数
    bool commandInfoValid = false;

    // 是否已经完成DRAM物理位置译码
    bool physicalLocationValid = false;
};


// ---------------------------------------------------------
// 唯一定位一个128-bit bank-local ECC codeword
// ---------------------------------------------------------
struct CodewordKey
{
    uint32_t channel = 0;
    uint32_t rank = 0;
    uint32_t chip = 0;
    uint32_t bank = 0;
    uint32_t row = 0;
    uint32_t column = 0;

    // 如果一次访问32B，则有两个128-bit ECC codeword
    uint8_t subBlock = 0;

    bool operator==(const CodewordKey &other) const
    {
        return channel == other.channel &&
               rank == other.rank &&
               chip == other.chip &&
               bank == other.bank &&
               row == other.row &&
               column == other.column &&
               subBlock == other.subBlock;
    }
};

struct CodewordKeyHash
{
    size_t operator()(const CodewordKey &k) const
    {
        size_t h = k.channel;
        h = h * 1315423911u + k.rank;
        h = h * 1315423911u + k.chip;
        h = h * 1315423911u + k.bank;
        h = h * 1315423911u + k.row;
        h = h * 1315423911u + k.column;
        h = h * 1315423911u + k.subBlock;
        return h;
    }
};

// ---------------------------------------------------------
// 一个正在执行的PIM transaction
// ---------------------------------------------------------
struct PimTxnContext
{
    uint64_t txnId = 0;

    PimAccessInfo access;

    // 当前命令实际发生的Bank读写次数。
    uint64_t bankReads = 0;
    uint64_t bankWrites = 0;

    uint32_t retryCount = 0;

    PimAlertLatch alert;
    bool underCorrection = false;

    Tick issueTick = 0;
    Tick correctionStartTick = 0;
};

} // namespace reliable_pim
} // namespace gem5

#endif