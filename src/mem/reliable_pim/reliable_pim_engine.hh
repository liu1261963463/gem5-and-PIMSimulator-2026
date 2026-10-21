#ifndef __MEM_RELIABLE_PIM_ENGINE_HH__
#define __MEM_RELIABLE_PIM_ENGINE_HH__

#include <functional>
#include <memory>
#include <unordered_map>

#include "base/statistics.hh"
#include "mem/reliable_pim/codeword_state.hh"
#include "mem/reliable_pim/pim_ecc.hh"
#include "mem/reliable_pim/rank_correction.hh"
#include "mem/reliable_pim/reliable_pim_types.hh"
#include "mem/reliable_pim/vrt_fault_model.hh"
#include "params/ReliablePIMEngine.hh"
#include "sim/eventq.hh"
#include "sim/sim_object.hh"

namespace gem5
{
namespace reliable_pim
{

class ReliablePIMEngine :
    public SimObject
{
  public:

    using Params =
        ReliablePIMEngineParams;

    ReliablePIMEngine(
        const Params &p);

    // PIM命令开始
    uint64_t
    beginPimCommand(
        const PimAccessInfo &access);

    // PIM命令执行完成，开始ECC检查
    PimCheckResult
    completePimCommand(
        uint64_t txnId);

    // 登记当前命令的一次实际Bank访问。
    void recordPimAccess(
        uint64_t txnId,
        const PimAccessInfo &access);

    // 由PIMSim注册callback
    void
    setRetryCallback(
        std::function<void(uint64_t)> cb);

    bool
    enabled() const
    {
        return enable;
    }

  private:

    void
    startCorrection(
        uint64_t txnId);

    void
    finishCorrection();

    CodewordKey
    makeCodewordKey(
        const PimAccessInfo &access,
        unsigned bank,
        unsigned subBlock) const;

  private:

    bool enable;

    bool codewordFlipEnabled;

    bool functionalEcc;

    CorrectionMode correctionMode;

    Tick rankCorrectionLatency;

    PimEccEngine ecc;

    VrtFaultModel vrt;

    CodewordStateTable codewordStates;

    RankCorrectionEngine rankCorrection;

    uint64_t nextTxnId = 0;

    std::unordered_map<
        uint64_t,
        PimTxnContext
    > activeTxns;

    uint64_t currentCorrectionTxn = 0;

    std::function<void(uint64_t)>
        retryCallback;

    EventFunctionWrapper
        correctionEvent;

    // -----------------------------
    // Stats
    // -----------------------------
    struct ReliablePIMStats :
        public statistics::Group
    {
        ReliablePIMStats(
            statistics::Group *parent);

        statistics::Scalar pimCommands;

        statistics::Scalar physicalReads;
        statistics::Scalar physicalWrites;
        statistics::Scalar physicalCodewordVisits;
        
        statistics::Scalar crcChecks;
        statistics::Scalar crcAlerts;

        statistics::Scalar rankCorrections;

        statistics::Scalar codewordFlips;

        statistics::Scalar pimRetries;

        statistics::Scalar correctionTicks;
    };

    ReliablePIMStats stats;
};

} // namespace reliable_pim
} // namespace gem5

#endif