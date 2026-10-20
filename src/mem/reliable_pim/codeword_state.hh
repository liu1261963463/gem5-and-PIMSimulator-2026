#ifndef __MEM_RELIABLE_PIM_CODEWORD_STATE_HH__
#define __MEM_RELIABLE_PIM_CODEWORD_STATE_HH__

#include <bitset>
#include <unordered_map>

#include "mem/reliable_pim/reliable_pim_types.hh"

namespace gem5
{
namespace reliable_pim
{

struct CodewordState
{
    // 是否在物理意义上已经执行过Codeword Flip
    //
    // 注意：
    // 该变量在bit-accurate模式中只能用于debug/oracle，
    // 不能让CRC decoder直接读取。
    bool physicalFlipped = false;

    // 128个数据bit中哪些是VRT weak cells
    std::bitset<128> weakMask;

    // weak cell容易在哪个逻辑值下出错
    // 第一版统一设为1 -> 0
    std::bitset<128> vulnerableValue;

    uint64_t correctionCount = 0;
    uint64_t flipCount = 0;
};


class CodewordStateTable
{
  public:

    CodewordState&
    get(const CodewordKey &key);

    bool
    exists(const CodewordKey &key) const;

    void
    flip(const CodewordKey &key);

    void
    clear();

    size_t
    size() const
    {
        return table.size();
    }

  private:

    std::unordered_map<
        CodewordKey,
        CodewordState,
        CodewordKeyHash
    > table;
};

} // namespace reliable_pim
} // namespace gem5

#endif