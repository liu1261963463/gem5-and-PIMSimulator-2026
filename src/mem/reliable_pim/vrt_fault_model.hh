#ifndef __MEM_RELIABLE_PIM_VRT_FAULT_MODEL_HH__
#define __MEM_RELIABLE_PIM_VRT_FAULT_MODEL_HH__

#include <cstdint>

#include "mem/reliable_pim/codeword_state.hh"
#include "mem/reliable_pim/pim_ecc.hh"

namespace gem5
{
namespace reliable_pim
{

class VrtFaultModel
{
  public:

    VrtFaultModel(
        double rate,
        uint64_t seed);

    void
    setRate(double rate);

    bool
    isWeakCell(
        const CodewordKey &key,
        unsigned bit) const;

    void
    apply(
        Codeword128 &cw,
        const CodewordKey &key,
        const CodewordState &state) const;

  private:

    double vrtRate;

    uint64_t seed;

    uint64_t
    hash(
        const CodewordKey &key,
        unsigned bit) const;
};

} // namespace reliable_pim
} // namespace gem5

#endif