# Fig. 12 前置验证：真实 GEMV 数据、分段计时和尺寸扫描

适配基线：`e16c1924f336b970218eabbc99e3cede7d12a011`。

本工具直接编译并调用仓库的 PIMSimulator 后端，不经过 gem5 的 CPU/MMIO 路径。
它运行当前 HBM2 后端，**不是论文 DDR5 Fig. 12 的数值复现**。
第十七步中的 JSON 仍然是参考参数清单；它不会将 HBM2 后端变成 DDR5。

## 安装及运行

把 `tools/fig12` 放在仓库根目录下。Linux 环境需要 g++、Python 3；绘图另需 matplotlib。
构建不需要 gtest，也不依赖旧版 libpimsim.so，不修改原来的 Sconstruct。
不要以 NO_STORAGE 模式编译：本工具必须实际存储、计算和校验数据。

在仓库根目录执行：

```bash
python3 tools/fig12/run.py build
python3 tools/fig12/run.py smoke --out results/fig12_smoke
python3 tools/fig12/run.py sweep --out results/fig12_sweep
python3 tools/fig12/run.py plot --out results/fig12_sweep
```

首次绘图若缺少 matplotlib，执行 `python3 -m pip install matplotlib`。
每次运行使用一个空的输出目录；重跑时换用 `results/fig12_sweep_2` 等新目录。
默认每组超时 600 秒，可用 `--timeout 1800` 调整。
完整扫描的最大矩阵有约 6700 万个元素，主机内存需求还包含模拟器数据和请求队列。
先跑 smoke，再开始完整扫描。日志持续写入每组的 `.log` 文件。

## 文件的作用

- `gemv_probe.cc`：构造非零 FP16 数据、调用现有 GEMV、读回每一行并验证、记录周期差。
- `run.py`：编译、批量运行、验证结果、输出 CSV、绘制诊断图。
- `README.md`：使用说明和测量边界。
- 补丁另修改 `src/mem/pimsim_wrapper.cc` 的 `executePIM()`：将 GEMV 固定返回 1000 周期改为 fatal，防止占位结果被用于实验。该改动不接通 MMIO GEMV。

若使用补丁已修改 wrapper，下一次运行 gem5 前执行 `scons build/X86/gem5.opt -j2`；上述独立后端工具不需要 gem5 构建。

## 校验设计

batch=1。输入矩阵与向量使用可重复的正负 1，参考结果由整数点积计算。
各 lane 的 FP16 部分和范围保证整数精确表示，读回后用 FP32 对 16 个 lane 归约。
读回缓冲区预填 NaN；缺失读回、错误行映射或结果错误都会终止当前测试。
校验包含补齐行。失败时不写该组结果 JSON，也不生成汇总 CSV。
这覆盖一类稠密非零输入，不替代一般 FP16 数值精度验证。

当前 kernel 的 preload 按完整 tile 遍历，没有自动处理最后不足一块的数据。
因此 M 补齐到 8*16*1*8=1024 的倍数，K 补齐到 256 的倍数；补齐区为零。
例如 M=768 会实际执行 1024 行。时间包含补齐开销，但 GFLOP/s 的分子只用有效工作量 2*M*K。

## 时间定义

同一个 PIMKernel 上读取四次累计周期计数，阶段边界调用 runPIM 排空请求。

1. weight_preload_cycles：将权重预置到 bank，单独报告；不计入下面两种稳态吞吐率。
2. kernel_including_upload_cycles：executeGemv 的全部命令，包括输入写入 GRF、模式切换、计算和结果存放。
3. result_read_cycles：读取部分和到主机缓冲区。

主机归约只用于校验，没有模拟其 CPU 执行时间；主机程序的墙钟时间不能冒充模拟时间。
阶段之间排空会阻止跨阶段重叠；诊断图只代表此明确定义的顺序执行流程。
权重装载会影响后续 bank 状态；这是预置权重后的单次测量，不是冷启动总延迟。

```text
useful_flops = 2*M*K
kernel_time_ns = kernel_including_upload_cycles * tCK_ns
read_time_ns = result_read_cycles * tCK_ns
kernel_including_upload_gflops = useful_flops / kernel_time_ns
kernel_plus_readback_gflops = useful_flops / (kernel_time_ns + read_time_ns)
```

FLOP/ns 数值上等于 GFLOP/s。乘加按两个 FLOP 计算。
不使用 gem5 整个程序的 simTicks 作为单次 GEMV 的耗时。

## 与论文 Fig. 12 的差距

论文 M={768,2048,3072,4096,8192}、K={2048,3072,4096,8192}，共 20 组；batch=1。
Fig. 12 是无故障性能实验，不是 VRT/纠错开销图（后者是 Fig. 13）。

当前测试不启用 ReliablePIMEngine，也不注入 VRT 故障。其数据只代表原 HBM2 后端。
诊断图的两组柱不是论文的 Without overheads/With overheads；不绘制猜测的 Optimal Rank PIM 线。
以下条件满足前不能把图改名为 Fig. 12 复现：

- DDR5-6400 完整时序、8 通道/每通道 4 rank、32 bank 及对应地址组织已经实现并验证。
- 四路 FP16 数据通路和 1 KB scratchpad 实际参与执行。
- Odd/Even 调度接入真实 kernel，而不只是独立调度测试。
- 定义并实现排除复制/归约开销的实验模式；不能直接把本工具 kernel 时间当作纯计算时间。
- 复制、读回、主机归约和所需传输的模拟时间已计入含开销模式，且不重复计数。
- 理想 rank-PIM 基线按论文相同系统范围与计数口径建立并核验。

## 结果追溯和排错

`manifest.json` 保存源文件、可执行文件、INI 的 SHA-256，以及完整用例列表。
修改源代码后必须重新 build；不要同时修改配置或并发启动多个扫描。
检查输出中的 `correct=true`、M/K、补齐尺寸和周期分段；命令退出 0 并不替代结果校验。
若出现 ERROR，先打开对应 M*_K*.log。失败不会继续生成成功图。

当前程序通过硬编码设备路径选择原来的 HBM2 配置。不要把论文参考 JSON 传给 INI 解析器。
