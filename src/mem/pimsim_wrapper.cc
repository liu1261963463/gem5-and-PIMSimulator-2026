/*
 * PIMSimulator/lib/half.h uses these optional macros in #if expressions.
 * gem5 enables -Wundef, so undefined macros will generate many warnings.
 * Define them explicitly before including any PIMSimulator headers.
 */
#ifndef HALF_ERRHANDLING_FLAGS
#define HALF_ERRHANDLING_FLAGS 0
#endif

#ifndef HALF_ERRHANDLING_ERRNO
#define HALF_ERRHANDLING_ERRNO 0
#endif

#ifndef HALF_ERRHANDLING_FENV
#define HALF_ERRHANDLING_FENV 0
#endif

#ifndef HALF_ERRHANDLING_THROWS
#define HALF_ERRHANDLING_THROWS 0
#endif

#ifndef HALF_ENABLE_F16C_INTRINSICS
#define HALF_ENABLE_F16C_INTRINSICS 0
#endif


#include "mem/pimsim_wrapper.hh"

#include "base/logging.hh"
#include "base/trace.hh"
#include "debug/PIMSim.hh"

#include "pim/PIMKernel.h"
#include "pim/PIMCmdGen.h"

#include <algorithm>
#include <cassert>
#include <cstdint>

#include "MultiChannelMemorySystem.h"
#include "Callback.h"
#include "Burst.h"

// PIMSimulator kernel headers.
// #include "tests/PIMKernel.h"

namespace gem5
{
namespace memory
{

static KernelType
toPIMKernelType(PimOpcode op)
{
    switch (op) {
      case PimOpcode::ADD:
        return KernelType::ADD;
      case PimOpcode::MUL:
        return KernelType::MUL;
      case PimOpcode::RELU:
        return KernelType::RELU;
      default:
        panic("Unsupported opcode for executeEltwise()");
    }
}

static pimBankType
toPIMBankType(PimBankSel bank)
{
    switch (bank) {
      case PimBankSel::ALL:
        return pimBankType::ALL_BANK;
      case PimBankSel::EVEN:
        return pimBankType::EVEN_BANK;
      case PimBankSel::ODD:
        return pimBankType::ODD_BANK;
      default:
        panic("Unsupported PIM bank selector");
    }
}

PIMSimWrapper::PIMSimWrapper(const std::string &device_config,
                             const std::string &system_config,
                             const std::string &file_path,
                             const std::string &trace_file,
                             unsigned int megs_of_memory,
                             bool enable_debug,
                             unsigned pim_chans,
                             unsigned pim_ranks)
    : pimChans(pim_chans),
      pimRanks(pim_ranks)
{
    const std::string dev = file_path + "/" + device_config;
    const std::string sys = file_path + "/" + system_config;

    pimsim = std::make_shared<DRAMSim::MultiChannelMemorySystem>(
        dev,
        sys,
        file_path,
        trace_file,
        megs_of_memory
    );

    _burstSize = 32;
    _clockPeriod = 1.0;
    _queueSize = 32;
}

PIMSimWrapper::~PIMSimWrapper()
{
}

void
PIMSimWrapper::setCallbacks(DRAMSim::TransactionCompleteCB *read_cb,
                            DRAMSim::TransactionCompleteCB *write_cb)
{
    pimsim->RegisterCallbacks(read_cb, write_cb, nullptr);
}

bool
PIMSimWrapper::canAccept() const
{
    return pimsim->willAcceptTransaction();
}

void
PIMSimWrapper::enqueue(bool is_write, uint64_t addr)
{
    DRAMSim::BurstType null_bst;
    bool ok = pimsim->addTransaction(is_write, addr, &null_bst);
    panic_if(!ok, "PIMSimulator cannot accept normal transaction");
}

void
PIMSimWrapper::tick()
{
    pimsim->update();
}

void
PIMSimWrapper::printStats()
{
    pimsim->printStats(true);
}

void
PIMSimWrapper::drainPIMSimulator()
{
    while (pimsim->hasPendingTransactions()) {
        pimsim->update();
    }
}

/**************************************************** */
// uint64_t
// PIMSimWrapper::executePIM(const PIMOp &op)
// {
//     /*
//      * 第一阶段先返回固定延迟，验证 MMIO 通路是否打通。
//      * 等 MMIO 通路验证成功后，再把这里替换成真正的 PIMSimulator
//      * PIMKernel::executeGemv / executeEltwise 调用。
//      */
//     switch (op.opcode) {
//       case PIMOpcode::Gemv:
//         return 1000;

//       case PIMOpcode::Add:
//         return 200;

//       case PIMOpcode::Mul:
//         return 200;

//       case PIMOpcode::Relu:
//         return 100;

//       default:
//         return 1;
//     }
/**************************************************** */

uint64_t
PIMSimWrapper::executePIM(const PIMOp &op)
{
    /*
     * 注意：
     * 这里假设你前面已经能够正常构造 PIMKernel。
     * 如果你的构造函数当前是：
     *
     *     PIMKernel kernel(pimsim);
     *
     * 并且没有报错，就保持不变。
     *
     * 如果后续报 PIMKernel 构造函数参数错误，再单独修改构造方式。
     */
    PIMKernel kernel(pimsim, pimChans, pimRanks);

    uint64_t begin_cycle = kernel.getCycle();

    switch (op.opcode) {
      case PIMOpcode::Gemv:
        /*
         * 当前不能这样调用：
         *
         *     kernel.executeGemv(op.src0, op.src1, op.dst, op.m, op.n, op.numTile);
         *
         * 因为 PIMSimulator 当前接口是：
         *
         *     executeGemv(NumpyBurstType* w_data,
         *                 NumpyBurstType* i_data,
         *                 bool is_tree)
         *
         * 也就是说，它需要真实的 NumpyBurstType 数据对象，
         * 不是 row / m / n 参数。
         *
         * 第一阶段先返回固定延迟，保证编译通过。
         */
        return 1000;

      case PIMOpcode::Add:
        /*
         * 正确接口：
         *
         * executeEltwise(
         *     int dim,
         *     pimBankType bank_types,
         *     KernelType ktype,
         *     int input0_row,
         *     int result_row,
         *     int input1_row
         * )
         *
         * 这里把 op.numTile 暂时当作 dim 使用。
         * 后面测试程序中不要传 16 这种 tile 数，
         * 而应该传真实元素维度，例如 1024 * 1024。
         */
        kernel.executeEltwise(
            static_cast<int>(op.numTile),
            pimBankType::ALL_BANK,
            KernelType::ADD,
            static_cast<int>(op.src0),
            static_cast<int>(op.dst),
            static_cast<int>(op.src1)
        );
        break;

      case PIMOpcode::Mul:
        kernel.executeEltwise(
            static_cast<int>(op.numTile),
            pimBankType::ALL_BANK,
            KernelType::MUL,
            static_cast<int>(op.src0),
            static_cast<int>(op.dst),
            static_cast<int>(op.src1)
        );
        break;

      case PIMOpcode::Relu:
        kernel.executeEltwise(
            static_cast<int>(op.numTile),
            pimBankType::ALL_BANK,
            KernelType::RELU,
            static_cast<int>(op.src0),
            static_cast<int>(op.dst),
            0
        );
        break;

      default:
        return 1;
    }

    /*
     * PIMKernel 的 executeEltwise() 只是生成 PIM transaction，
     * 真正推进 PIMSimulator 时序需要 runPIM()。
     */
    kernel.runPIM();

    uint64_t end_cycle = kernel.getCycle();

    if (end_cycle <= begin_cycle) {
        return 1;
    }

    return end_cycle - begin_cycle;
}
  

uint64_t
PIMSimWrapper::executePimCommand(const PimCommand &cmd)
{
    drainPIMSimulator();

    PIMKernel kernel(pimsim, pimChans, pimRanks);

    switch (cmd.opcode) {
      case PimOpcode::ADD:
      case PimOpcode::MUL:
      case PimOpcode::RELU: {
        const KernelType ktype = toPIMKernelType(cmd.opcode);
        const pimBankType btype = toPIMBankType(cmd.bank);

        DPRINTF(PIMSim,
                "Launch PIM Eltwise: opcode=%lu dim=%lu "
                "input0Row=%u input1Row=%u resultRow=%u bank=%lu\n",
                static_cast<uint64_t>(cmd.opcode),
                cmd.dim,
                cmd.input0Row,
                cmd.input1Row,
                cmd.resultRow,
                static_cast<uint64_t>(cmd.bank));

        kernel.executeEltwise(
            cmd.dim,
            btype,
            ktype,
            cmd.input0Row,
            cmd.resultRow,
            cmd.input1Row
        );

        kernel.runPIM();
        return kernel.getCycle();
      }

      case PimOpcode::GEMV: {
        /*
         * 第一版建议只用于 GEMV timing 验证。
         * 如果要功能正确性，需要把 guest memory 中的 W/X 数据复制到
         * NumpyBurstType，再调用 preloadGemv() + executeGemv() + readResult()。
         */
        DPRINTF(PIMSim,
                "Launch PIM GEMV dummy timing: batch=%u inputDim=%u outputDim=%u\n",
                cmd.batch, cmd.inputDim, cmd.outputDim);

        NumpyBurstType weight;
        NumpyBurstType input;

        weight.shape.push_back(cmd.outputDim);
        weight.shape.push_back(cmd.inputDim);
        weight.loadTobShape(16);

        input.shape.push_back(cmd.batch);
        input.shape.push_back(cmd.inputDim);
        input.loadTobShape(16);

        DRAMSim::BurstType zero;
        zero.set((float)0);

        const size_t weight_bursts =
            static_cast<size_t>(weight.bShape[0]) *
            static_cast<size_t>(weight.bShape[1]);

        const size_t input_bursts =
            static_cast<size_t>(input.bShape[0]) *
            static_cast<size_t>(input.bShape[1]);

        for (size_t i = 0; i < weight_bursts; ++i) {
            weight.bData.push_back(zero);
        }

        for (size_t i = 0; i < input_bursts; ++i) {
            input.bData.push_back(zero);
        }

        kernel.preloadGemv(&weight);
        kernel.executeGemv(&weight, &input, false);
        kernel.runPIM();

        return kernel.getCycle();
      }

      case PimOpcode::NOP:
      default:
        return 0;
    }
}

}
}