# Replication 与 Reduction 的计时口径

基线提交：d3981fca792301536146412a2a4d9ed52bb5251b。
本工具复用已有完整 HBM2 GEMV 实测结果，新增显式的主机归约时间估算。
不改变 gem5/PIMSimulator 模拟时间，不是完整 DDR5 Fig.12 复现。

## 使用

在仓库根目录执行：

```bash
python3 tools/fig12/test_account_overheads.py
python3 tools/fig12/account_overheads.py \
    --input PIMReliable_results/fig12_sweep \
    --out PIMReliable_results/fig12_accounted_example \
    --host-reduction-ns-per-row 5 \
    --host-reduction-setup-ns 0 \
    --host-model-label illustrative-not-calibrated-5ns-per-row \
    --plot
```

**5 ns/行只是检查流程的示例假设，不是论文值，也不是 gem5 CPU 测量值。**
脚本不提供默认每行时间，必须明确输入并用标签记录来源。
如缺少 matplotlib，先执行 `python3 -m pip install matplotlib`，或省略 `--plot`。
输出目录应为空；改用另一个场景时指定新目录。
已有 20 组结果不必重新模拟；不需要重新编译 gem5 或 libpimsim。

生成 accounted.csv、accounting_manifest.json，以及可选 PNG。
不会覆盖原来的 JSON、CSV、manifest 或图。

## 为什么复制不能再加一次

`ext/pimsim/PIMSimulator/src/pim/PIMKernel.cpp` 中 `computeGemv()` 的
`WRIO_TO_GRF_` 请求，在各 channel/rank 上传输入，并经共享的 GRF 广播路径
送到该 rank 内的 PIM blocks。请求已经在 executeGemv/runPIM 阶段执行。
因此 `kernel_including_upload_cycles` 包含这段复制，不能再按 bytes/bandwidth
增加一个复制时间，也不能减去某个估算值来冒充纯计算周期。

对于已核对的配置（16 channel、1 rank/channel、8 PIM blocks/rank）：

```text
输出 tile 数 = M_padded / 1024
输入 burst 数 = K_padded / 16
上传事务数 = 输出 tile 数 × 输入 burst 数 × 16
上传字节数 = 上传事务数 × 32
结果读回字节数 = M_padded × 32
```

这些是从已核对的循环和布局**推导**的流量，不是新增硬件计数器，不包含命令总线流量。
不要再乘 8：八个 PIMBlock 的内部广播不是额外八份外部上传事务。
脚本检查源文件和 INI 哈希指纹，配置/实现改变后会拒绝沿用公式。
哈希记录用于识别受支持的测量版本，不是安全签名。

## 为什么归约还需要增加时间

原 probe 读回每个输出的 16 个 FP16 部分和，使用 float 累加。
它运行在执行模拟器的主机上，没有推进模拟的 gem5 CPU 时间。
不能直接使用 C++ 程序的墙钟耗时填入目标 CPU 的仿真时间。

这里归约的是当前 HBM 内核每行的 16 个 lane 部分和，不是凭空假定有 16 个 bank 或 rank。
未来若改变矩阵分布并需要额外跨 bank/rank 归约，必须同时更新通信和计算模型。

原代码从 sum=0 开始执行 16 次加法；从数学上最少只需 15 次，
但本脚本报告原源代码的 16 次加法和 16 次转换，并不声称这是机器指令数。
归约遍历全部 M_padded 行，包含补零行；有效 FLOP 数仍只取 2*M*K。

主机估算模型：

```text
T_host = setup_ns + M_padded × ns_per_row
```

ns_per_row 是每行归约的摊销时间，包括本地缓冲区读取、FP16→FP32 转换、
累加，以及最终 FP32 结果保存；setup_ns 每个 GEMV 只计一次。
它不再包含 PIM→主机的结果传输，该部分已在 result_read_cycles 中。
模型假设各阶段顺序执行，没有 CPU/PIM 重叠、异步流水或排队争用。
归约结果在主机侧，若后续还要写回 PIM，需另外模拟该写回，当前不含此项。

## 总延迟与吞吐率

```text
T_backend = (kernel_including_upload_cycles + result_read_cycles) × tCK_ns
T_resident = T_backend + T_host
T_cold = weight_preload_cycles × tCK_ns + T_resident
GFLOP/s = (2*M*K) / T_ns
```

resident 假设权重已预置，适合讨论权重驻留后的单次 GEMV。
cold 加上本 probe 实际模拟的权重预置阶段；不代表完整应用启动、文件读取或所有主机准备工作。
两者分列，避免把权重装载算进去后仍称为驻留权重的性能。
绘图只比较 T_backend 与 T_resident；冷权重场景在 CSV 中。

## 正式实验如何获得归约参数

必须选定目标 CPU、频率、线程数、向量化实现、缓冲区大小和缓存状态。
在该目标 CPU 的测量或 gem5 CPU 仿真中，对同样的 16 路 FP16→FP32 归约
单独计时，并将启动开销与每行摊销开销分开。参考答案检查不应计入算子时间。
应检查不同 M 下线性模型是否成立；若缓存、并行度或带宽造成明显非线性，
需要按规模建立模型，不能强行沿用一个常数。
在完成校准前，结果只用于参数敏感性分析；不得声称已模拟主机 CPU 或复现论文曲线。

`host_reduction_timed_in_simulator=false` 会保留，另以
`host_reduction_modeled=true` 说明归约开销已经计入估算总延迟。
