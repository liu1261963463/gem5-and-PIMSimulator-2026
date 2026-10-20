#include "mem/reliable_pim/codeword_state.hh"

namespace gem5
{
namespace reliable_pim
{

CodewordState&
CodewordStateTable::get(const CodewordKey &key)
{
    return table[key];
}


bool
CodewordStateTable::exists(const CodewordKey &key) const
{
    return table.find(key) != table.end();
}


void
CodewordStateTable::flip(const CodewordKey &key)
{
    auto &state = table[key];

    state.physicalFlipped =
        !state.physicalFlipped;

    state.flipCount++;
}


void
CodewordStateTable::clear()
{
    table.clear();
}

} // namespace reliable_pim
} // namespace gem5