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
            // fatal_if(
            //     enable && (p.vrt_rate != 0.0 || functionalEcc),
            //     "This physical-access capture stage requires "
            //     "vrt_rate=0 and functional_ecc=false; "
            //     "fault recovery is not integrated");
            fatal_if(
            enable &&
                (p.vrt_rate != 0.0 ||
                functionalEcc ||
                codewordFlipEnabled),
            "This physical-access capture stage requires "
            "vrt_rate=0, functional_ecc=false and "
            "codeword_flip=false; "
            "fault recovery is not integrated");
                
            DPRINTF(
                ReliablePIM,
                "ReliablePIM enabled=%d "
                "VRT=%e\n",
                enable,
                p.vrt_rate);
        }

        // uint64_t
        // ReliablePIMEngine::beginPimCommand(
        //     const PimAccessInfo &access)
        // {
        //     if (!enable)
        //         return 0;

        //     const uint64_t id =
        //         nextTxnId++;
        uint64_t
        ReliablePIMEngine::beginPimCommand(
            const PimAccessInfo &access)
        {
            if (!enable)
                return 0;

            // 任务入口必须提供命令信息。
            // 物理位置随后由 recordPimAccess() 逐次登记。
            fatal_if(
                !access.commandInfoValid,
                "ReliablePIM: command metadata is missing.");

            fatal_if(!alertNHigh(),
                "ReliablePIM: unresolved ALERT_n blocks new commands.");

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

        bool
        ReliablePIMEngine::alertNHigh() const
        {
            for (const auto &entry : activeTxns)
                if (entry.second.alert.pending())
                    return false;
            return true;
        }

        PimCheckResult
        ReliablePIMEngine::reportCrcResult(
            uint64_t txnId, const DualCRC8Result &decoded)
        {
            fatal_if(!enable, "ReliablePIM: CRC report while disabled.");
            auto it = activeTxns.find(txnId);
            fatal_if(it == activeTxns.end(),
                "ReliablePIM: CRC report for unknown transaction.");
            fatal_if(!decoded.supported || !decoded.formatValid,
                "ReliablePIM: invalid CRC report is not a memory alert.");

            const auto expected =
                decoded.normalValid && !decoded.flippedValid
                    ? DecodeResult::NORMAL
                    : (!decoded.normalValid && decoded.flippedValid
                        ? DecodeResult::FLIPPED : DecodeResult::ALERT);
            fatal_if(decoded.result != expected,
                "ReliablePIM: inconsistent dual CRC result.");

            ++stats.crcChecks;
            const bool detected = decoded.result == DecodeResult::ALERT;
            if (detected)
                ++stats.crcAlerts;

            const bool wasHigh = alertNHigh();
            it->second.alert.observe(detected);
            if (wasHigh && !alertNHigh())
            {
                ++stats.alertAssertions;
                DPRINTF(ReliablePIM,
                    "ALERT_N_ASSERT txn=%llu level=0\n",
                    static_cast<unsigned long long>(txnId));
            }
            return it->second.alert.pending()
                ? PimCheckResult::STALL_FOR_CORRECTION
                : PimCheckResult::SUCCESS;
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

        void
        ReliablePIMEngine::recordPimAccess(
            uint64_t txnId,
            const PimAccessInfo &access)
        {
            // 1. Engine关闭时，不应该收到位置登记。
            fatal_if(
                !enable,
                "ReliablePIM: access while engine disabled");

            // 2. 找到这次访问所属的PIM命令。
            auto it = activeTxns.find(txnId);

            fatal_if(
                it == activeTxns.end(),
                "ReliablePIM: unknown transaction");

            // 3. 验证位置和Bank编号。
            fatal_if(
                !access.physicalLocationValid || access.bank >= 32,
                "ReliablePIM: invalid physical location or bank");

            // 每次底层回调只描述一个实际Bank。
            fatal_if(
                access.activeBanks.count() != 1 ||
                !access.activeBanks.test(access.bank),
                "ReliablePIM: invalid bank-local access mask");

            auto &ctx = it->second;

            // 4. 更新单个命令和Engine累计的读写次数。
            if (access.isWrite)
            {
                ++ctx.bankWrites;
                ++stats.physicalWrites;
            }
            else
            {
                ++ctx.bankReads;
                ++stats.physicalReads;
            }

            // 5. 当前每次访问为32B，对应两个128-bit码字。
            // 这里只定位码字并记录访问次数，不执行CRC解码。
            for (unsigned sub = 0; sub < 2; ++sub)
            {
                const auto key =
                    makeCodewordKey(access, access.bank, sub);

                // 查找或创建这个真实位置对应的码字状态。
                codewordStates.get(key);

                ++stats.physicalCodewordVisits;
            }

            // 6. 打印本次实际Bank访问。
            DPRINTF(
                ReliablePIM,
                "BANK_ACCESS txn=%llu "
                "ch=%u rank=%u bank=%u "
                "row=%u col=%u write=%d bytes=32 "
                "trigger=%#llx addrValid=0\n",
                static_cast<unsigned long long>(txnId),
                access.channel,
                access.rank,
                access.bank,
                access.row,
                access.column,
                access.isWrite,
                static_cast<unsigned long long>(access.triggerAddr));
        }

        

        PimCheckResult
        ReliablePIMEngine::completePimCommand(uint64_t txnId)
        {
            if (!enable)
                return PimCheckResult::SUCCESS;

            // 1. 找到当前命令。
            auto it = activeTxns.find(txnId);

            fatal_if(
                it == activeTxns.end(),
                "ReliablePIM: unknown transaction");

            const auto &ctx = it->second;

            // Keep the transaction and alarm until real recovery exists.
            if (ctx.alert.pending())
            {
                DPRINTF(ReliablePIM,
                    "ALERT_N_PENDING txn=%llu level=0\n",
                    static_cast<unsigned long long>(txnId));
                return PimCheckResult::STALL_FOR_CORRECTION;
            }

            // 2. 支持的PIM测试应当产生实际Bank访问。
            // 没有访问时明确报错，避免空检查被当成成功。
            fatal_if(
                ctx.bankReads + ctx.bankWrites == 0,
                "ReliablePIM: no Bank accesses captured; "
                "check NO_STORAGE and the kernel dimensions");

            // 3. 输出当前命令的访问汇总。
            DPRINTF(
                ReliablePIM,
                "BANK_SUMMARY txn=%llu reads=%llu writes=%llu\n",
                static_cast<unsigned long long>(txnId),
                static_cast<unsigned long long>(ctx.bankReads),
                static_cast<unsigned long long>(ctx.bankWrites));

            // 4. 命令结束，删除事务上下文。
            activeTxns.erase(it);

            // 这里表示位置采集流程完成，不表示CRC验证通过。
            return PimCheckResult::SUCCESS;
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

        // void
        // ReliablePIMEngine::finishCorrection()
        // {
        //     const uint64_t txnId =
        //         currentCorrectionTxn;

        //     auto it =
        //         activeTxns.find(txnId);

        //     if (it == activeTxns.end())
        //         return;

        //     auto &ctx = it->second;

        //     stats.correctionTicks +=
        //         curTick() -
        //         ctx.correctionStartTick;

        //     /*
        //     * 第一版简单处理：
        //     *
        //     * 找到第一个active bank，
        //     * 对它的两个128-bit block执行Flip。
        //     *
        //     * 第二版RankCorrectionFSM会真正告诉我们
        //     * 哪个bank/chip发生故障。
        //     */

        //     if (codewordFlipEnabled)
        //     {
        //         for (unsigned bank = 0;
        //             bank < 32;
        //             ++bank)
        //         {
        //             if (!ctx.access
        //                     .activeBanks
        //                     .test(bank))
        //             {
        //                 continue;
        //             }

        //             for (unsigned sub = 0;
        //                 sub < 2;
        //                 ++sub)
        //             {
        //                 const auto key =
        //                     makeCodewordKey(
        //                         ctx.access,
        //                         bank,
        //                         sub);

        //                 codewordStates.flip(key);

        //                 stats.codewordFlips++;
        //             }

        //             // 第一版只处理第一个fault bank
        //             break;
        //         }
        //     }

        //     ctx.retryCount++;

        //     stats.pimRetries++;

        //     ctx.underCorrection = false;

        //     DPRINTF(
        //         ReliablePIM,
        //         "Correction complete "
        //         "txn=%lu retry=%u\n",
        //         txnId,
        //         ctx.retryCount);

        //     if (retryCallback)
        //     {
        //         retryCallback(txnId);
        //     }
        // }

        void
        ReliablePIMEngine::finishCorrection()
        {
            fatal(
                "ReliablePIM: recovery is not implemented; "
                "real correction and physical codeword writeback "
                "must precede retry.");
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
                physicalReads,
                statistics::units::Count::get(),
                "Observed 32-byte Bank reads during PIM commands"),

            ADD_STAT(
                physicalWrites,
                statistics::units::Count::get(),
                "Observed 32-byte Bank writes during PIM commands"),

            ADD_STAT(
                physicalCodewordVisits,
                statistics::units::Count::get(),
                "Observed 16-byte codeword visits, not ECC checks"),

            ADD_STAT(
                crcChecks,
                statistics::units::Count::get(),
                "Number of supported codeword decode reports"),

            ADD_STAT(
                crcAlerts,
                statistics::units::Count::get(),
                "Number of decode reports classified ALERT"),

            ADD_STAT(
                alertAssertions,
                statistics::units::Count::get(),
                "Number of engine aggregate ALERT_n high-to-low transitions"),

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


