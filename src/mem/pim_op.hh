#ifndef __MEM_PIM_OP_HH__
#define __MEM_PIM_OP_HH__

#include <cstdint>

#include "base/types.hh"

namespace gem5
{
namespace memory
{

enum class PIMOpcode : uint64_t
{
    Nop  = 0,
    Gemv = 1,
    Add  = 2,
    Mul  = 3,
    Relu = 4
};

struct PIMOp
{
    PIMOpcode opcode = PIMOpcode::Nop;

    /*
     * 对 PIMSimulator 来说，这里建议第一版直接传 row/tile 信息，
     * 因为 PIMSimulator 的 PIMKernel.cpp 本身就是按 input_row、
     * result_row、num_tile 等参数组织 PIM 操作。
     *
     * 后续如果你实现了地址到 row/bank/col 的映射，也可以把这里
     * 改成真实物理地址。
     */
    uint64_t src0 = 0;
    uint64_t src1 = 0;
    uint64_t dst  = 0;

    uint64_t numTile = 0;

    uint32_t m = 0;
    uint32_t n = 0;
    uint32_t k = 0;
};

} // namespace memory
} // namespace gem5

#endif