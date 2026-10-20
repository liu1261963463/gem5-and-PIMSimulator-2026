#include "mem/reliable_pim/reliable_pim_engine.hh"

#include "base/logging.hh"
#include "debug/ReliablePIM.hh"

namespace gem5
{
namespace reliable_pim
        {

        static EccMode
        parseEccMode(const std::string &mode)
        {
            if (mode == "CRC8")
                return EccMode::CRC8;

            if (mode == "CRC16")
                return EccMode::CRC16;

            if (mode == "SEC")
                return EccMode::SEC;

            return EccMode::NONE;
        }


        static CorrectionMode
        parseCorrectionMode(
            const std::string &mode)
        {
            if (mode == "SW")
                return CorrectionMode::SOFTWARE;

            return CorrectionMode::HARDWARE;
        }

        ReliablePIMEngine::ReliablePIMEngine(
            const Params &p)
            : SimObject(p),

            enable(p.enable),

            codewordFlipEnabled(
                p.codeword_flip),

            functionalEcc(
                p.functional_ecc),

            correctionMode(
                parseCorrectionMode(
                    p.correction_mode)),

            rankCorrectionLatency(
                p.rank_correction_latency),

            ecc(
                parseEccMode(
                    p.ecc_mode)),

            vrt(
                p.vrt_rate,
                p.random_seed),

            correctionEvent(
                [this] {
                    finishCorrection();
                },
                name() + ".correctionEvent"),

            stats(this)
        {
            DPRINTF(
                ReliablePIM,
                "ReliablePIM enabled=%d "
                "VRT=%e\n",
                enable,
                p.vrt_rate);
        }

        uint64_t
        ReliablePIMEngine::beginPimCommand(
            const PimAccessInfo &access)
        {
            if (!enable)
                return 0;

            const uint64_t id =
                nextTxnId++;

            PimTxnContext ctx;

            ctx.txnId = id;
            ctx.access = access;
            ctx.issueTick = curTick();

            activeTxns.emplace(
                id,
                ctx);

            stats.pimCommands++;

            DPRINTF(
                ReliablePIM,
                "Begin PIM txn=%lu "
                "rank=%u bankmask=%s\n",
                id,
                access.rank,
                access.activeBanks
                    .to_string());

            return id;
        }

        CodewordKey
        ReliablePIMEngine::makeCodewordKey(
            const PimAccessInfo &access,
            unsigned bank,
            unsigned subBlock) const
        {
            CodewordKey key;

            key.channel =
                access.channel;

            key.rank =
                access.rank;

            key.chip =
                access.chip;

            key.bank =
                bank;

            key.row =
                access.row;

            key.column =
                access.column;

            key.subBlock =
                subBlock;

            return key;
        }

        PimCheckResult
        ReliablePIMEngine::completePimCommand(
            uint64_t txnId)
        {
            if (!enable)
                return PimCheckResult::SUCCESS;

            auto it =
                activeTxns.find(txnId);

            if (it == activeTxns.end())
            {
                fatal(
                    "Unknown ReliablePIM txn %lu",
                    txnId);
            }

            auto &ctx = it->second;

            // 未取得实际物理位置时，只完成命令接线验证。
            // 不创建码字状态，也不增加crcChecks。
            if (!ctx.access.physicalLocationValid)
            {
                warn_once(
                    "ReliablePIM: physical location is unavailable; "
                    "skipping reliability checks. "
                    "This run validates command integration only.");

                activeTxns.erase(it);

                return PimCheckResult::SUCCESS;
            }

            // 已声明位置有效，却没有任何参与Bank，属于接线错误。
            // fatal_if(
            //     ctx.access.activeBanks.none(),
            //     "ReliablePIM: physicalLocationValid is true, "
            //     "but activeBanks is empty.");
            // 等真实地址映射接好、开始正式可靠性实验时，建议将上述“警告并跳过”分支换成严格检查：
            fatal_if(
                !ctx.access.physicalLocationValid,
                "ReliablePIM: reliability evaluation requires "
                "a valid physical location.");

            bool detectedError = false;

            for (unsigned bank = 0;
                bank < 32;
                ++bank)
            {
                if (!ctx.access.activeBanks.test(bank))
                    continue;

                // 32B = 两个128-bit ECC block
                for (unsigned sub = 0;
                    sub < 2;
                    ++sub)
                {
                    const auto key =
                        makeCodewordKey(
                            ctx.access,
                            bank,
                            sub);

                    auto &state =
                        codewordStates.get(key);

                    stats.crcChecks++;

                    /*
                    * 第一版 timing 模型：
                    *
                    * 如果当前128 bits中出现VRT weak cell，
                    * 且没有被Codeword Flip mask，
                    * 就认为bank CRC报警。
                    *
                    * 后续functional模式再真正调用
                    * ecc.dualDecode(codeword)。
                    */

                    for (unsigned bit = 0;
                        bit < 128;
                        ++bit)
                    {
                        if (!vrt.isWeakCell(
                                key, bit))
                            continue;

                        // Codeword Flip以后，
                        // 第一版假设该单个VRT被mask
                        if (state.physicalFlipped)
                            continue;

                        detectedError = true;

                        break;
                    }

                    if (detectedError)
                        break;
                }

                if (detectedError)
                    break;
            }

            if (!detectedError)
            {
                activeTxns.erase(txnId);

                return PimCheckResult::SUCCESS;
            }

            stats.crcAlerts++;

            ctx.alertRaised = true;

            startCorrection(txnId);

            return
                PimCheckResult::
                STALL_FOR_CORRECTION;
        }

        void
        ReliablePIMEngine::startCorrection(
            uint64_t txnId)
        {
            auto &ctx =
                activeTxns.at(txnId);

            ctx.underCorrection = true;

            ctx.correctionStartTick =
                curTick();

            currentCorrectionTxn =
                txnId;

            stats.rankCorrections++;

            DPRINTF(
                ReliablePIM,
                "Start correction txn=%lu\n",
                txnId);

            /*
            * 第一版：
            * 使用论文baseline correction latency。
            *
            * 第二版：
            * 这里替换成真正的
            * rankCorrection.start(...)
            */
            schedule(
                correctionEvent,
                curTick() +
                rankCorrectionLatency);
        }

        void
        ReliablePIMEngine::finishCorrection()
        {
            const uint64_t txnId =
                currentCorrectionTxn;

            auto it =
                activeTxns.find(txnId);

            if (it == activeTxns.end())
                return;

            auto &ctx = it->second;

            stats.correctionTicks +=
                curTick() -
                ctx.correctionStartTick;

            /*
            * 第一版简单处理：
            *
            * 找到第一个active bank，
            * 对它的两个128-bit block执行Flip。
            *
            * 第二版RankCorrectionFSM会真正告诉我们
            * 哪个bank/chip发生故障。
            */

            if (codewordFlipEnabled)
            {
                for (unsigned bank = 0;
                    bank < 32;
                    ++bank)
                {
                    if (!ctx.access
                            .activeBanks
                            .test(bank))
                    {
                        continue;
                    }

                    for (unsigned sub = 0;
                        sub < 2;
                        ++sub)
                    {
                        const auto key =
                            makeCodewordKey(
                                ctx.access,
                                bank,
                                sub);

                        codewordStates.flip(key);

                        stats.codewordFlips++;
                    }

                    // 第一版只处理第一个fault bank
                    break;
                }
            }

            ctx.retryCount++;

            stats.pimRetries++;

            ctx.underCorrection = false;

            DPRINTF(
                ReliablePIM,
                "Correction complete "
                "txn=%lu retry=%u\n",
                txnId,
                ctx.retryCount);

            if (retryCallback)
            {
                retryCallback(txnId);
            }
        }

        ReliablePIMEngine::
        ReliablePIMStats::
        ReliablePIMStats(
            statistics::Group *parent)
            : statistics::Group(parent),

            ADD_STAT(
                pimCommands,
                statistics::units::Count::get(),
                "Number of PIM commands"),

            ADD_STAT(
                crcChecks,
                statistics::units::Count::get(),
                "Number of CRC checks"),

            ADD_STAT(
                crcAlerts,
                statistics::units::Count::get(),
                "Number of CRC alerts"),

            ADD_STAT(
                rankCorrections,
                statistics::units::Count::get(),
                "Number of rank corrections"),

            ADD_STAT(
                codewordFlips,
                statistics::units::Count::get(),
                "Number of codeword flips"),

            ADD_STAT(
                pimRetries,
                statistics::units::Count::get(),
                "Number of PIM retries"),

            ADD_STAT(
                correctionTicks,
                statistics::units::Tick::get(),  //如果你的25.1分支对某个units::Tick命名报错，可以参考同版本已有Stats代码，把它换成：statistics::units::Cycle::get()
                "Ticks spent in rank correction")
        {
        }
    }
}


