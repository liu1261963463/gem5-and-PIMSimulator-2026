#ifndef __MEM_PIM_CMD_HH__
#define __MEM_PIM_CMD_HH__

#include <cstdint>

namespace gem5
{
namespace memory
{

enum class PimOpcode : uint64_t
{
    NOP  = 0,
    ADD  = 1,
    MUL  = 2,
    RELU = 3,
    GEMV = 4,
};

enum class PimBankSel : uint64_t
{
    ALL  = 0,
    EVEN = 1,
    ODD  = 2,
};

struct PimCommand
{
    PimOpcode opcode = PimOpcode::NOP;

    // Eltwise 操作使用 dim 表示 burst 数量或 block 化后的维度。
    uint64_t dim = 0;

    // 对应 PIMSimulator row-based API。
    uint32_t input0Row = 0;
    uint32_t input1Row = 0;
    uint32_t resultRow = 0;

    PimBankSel bank = PimBankSel::ALL;

    // GEMV dummy timing path 使用。
    uint32_t batch = 1;
    uint32_t inputDim = 0;
    uint32_t outputDim = 0;
};

}
}

#endif