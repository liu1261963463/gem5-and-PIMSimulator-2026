# configs/myconfigs/x86-fs-custom.py

from gem5.components.boards.x86_board import X86Board
from gem5.components.cachehierarchies.classic.private_l1_shared_l2_cache_hierarchy import (
    PrivateL1SharedL2CacheHierarchy,
)
from gem5.components.memory.single_channel import SingleChannelDDR3_1600
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.components.processors.cpu_types import CPUTypes
from gem5.isas import ISA
from gem5.resources.resource import DiskImageResource, KernelResource
from gem5.simulate.simulator import Simulator
from gem5.utils.requires import requires


# 1. 要求当前 gem5 是 X86 ISA
requires(isa_required=ISA.X86)


# 2. 配置 cache hierarchy
cache_hierarchy = PrivateL1SharedL2CacheHierarchy(
    l1d_size="32KiB",
    l1i_size="32KiB",
    l2_size="512KiB",
)


# 3. 配置内存
memory = SingleChannelDDR3_1600(size="2GiB")


# 4. 配置 CPU
# 前期调试建议使用 ATOMIC，速度快；
# 后期性能统计可以改成 TIMING 或 O3。
processor = SimpleProcessor(
    cpu_type=CPUTypes.ATOMIC,
    isa=ISA.X86,
    num_cores=1,
)


# 5. 创建 X86 full-system board
board = X86Board(
    clk_freq="3GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)


# 6. 指定你的 disk image
# 如果 root 分区是 /dev/sda1，则 root_partition="1"；
# 如果 root 分区是 /dev/sda2，则改成 root_partition="2"。
disk = DiskImageResource(
    local_path="/home/wangchengcheng/gem5-stable/util/disk-image-validator/x86-ubuntu.img",
    root_partition="1",
)


# 7. 指定 Linux kernel
# 这里需要你提供一个 x86 kernel。
# 例如你可以把 kernel 放到：
# /home/wangchengcheng/gem5-system/binaries/vmlinux
kernel = KernelResource(
    local_path="/home/wangchengcheng/gem5-system/binaries/vmlinux",
    architecture=ISA.X86,
)


# 8. 设置 full-system workload
# 这里的 readfile_contents 会在 guest Linux 启动后执行。
board.set_kernel_disk_workload(
    kernel=kernel,
    disk_image=disk,
    readfile_contents="""
echo "===== gem5 guest started ====="

cd /root/gem5_tests

echo "Current directory:"
pwd

echo "Files in /root/gem5_tests:"
ls -lh

/sbin/m5 resetstats

./test_program > test_program.out 2>&1

/sbin/m5 dumpstats

cat test_program.out

/sbin/m5 exit
""",
)


# 9. 创建并启动模拟器
simulator = Simulator(board=board)

print("Beginning simulation!")
simulator.run()
print("Simulation finished!")