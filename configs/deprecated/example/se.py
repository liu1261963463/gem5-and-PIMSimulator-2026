# Copyright (c) 2012-2013 ARM Limited
# All rights reserved.
#
# The license below extends only to copyright in the software and shall
# not be construed as granting a license to any other intellectual
# property including but not limited to intellectual property relating
# to a hardware implementation of the functionality of the software
# licensed hereunder.  You may use the software subject to the license
# terms below provided that you ensure that this notice is replicated
# unmodified and in its entirety in all distributions of the software,
# modified or unmodified, in source code or in binary form.
#
# Copyright (c) 2006-2008 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Simple test script
#
# "m5 test.py"

import argparse
import os
import sys

import m5


from m5.defines import buildEnv
from m5.objects import *
from m5.params import NULL
from m5.util import (
    addToPath,
    fatal,
    warn,
)

from gem5.isas import ISA

addToPath("../../")

from common import (
    CacheConfig,
    CpuConfig,
    MemConfig,
    ObjectList,
    Options,
    Simulation,
)
from common.Caches import *
from common.cpu2000 import *
from common.FileSystemConfig import config_filesystem
from ruby import Ruby





def get_processes(args):
    """Interprets provided args and returns a list of processes"""

    multiprocesses = []
    inputs = []
    outputs = []
    errouts = []
    pargs = []

    workloads = args.cmd.split(";")
    if args.input != "":
        inputs = args.input.split(";")
    if args.output != "":
        outputs = args.output.split(";")
    if args.errout != "":
        errouts = args.errout.split(";")
    if args.options != "":
        pargs = args.options.split(";")

    idx = 0
    for wrkld in workloads:
        process = Process(pid=100 + idx)
        process.executable = wrkld
        process.cwd = os.getcwd()
        process.gid = os.getgid()
        
        # ######################################################333
        # if args.enable_pim_ctrl:
        #    process.map(args.pim_se_va, args.pim_mmio_base, 0x1000, False)

        #    system.cpu.workload = process
        #    system.cpu.createThreads()
        # ######################################################333

        if args.env:
            with open(args.env) as f:
                process.env = [line.rstrip() for line in f]

        if len(pargs) > idx:
            process.cmd = [wrkld] + pargs[idx].split()
        else:
            process.cmd = [wrkld]

        if len(inputs) > idx:
            process.input = inputs[idx]
        if len(outputs) > idx:
            process.output = outputs[idx]
        if len(errouts) > idx:
            process.errout = errouts[idx]

        multiprocesses.append(process)
        idx += 1

    if args.smt:
        cpu_type = ObjectList.cpu_list.get(args.cpu_type)
        assert ObjectList.is_o3_cpu(cpu_type), "SMT requires an O3CPU"
        return multiprocesses, idx
    else:
        return multiprocesses, 1


warn(
    "The se.py script is deprecated. It will be removed in future releases of "
    " gem5."
)

parser = argparse.ArgumentParser()
Options.addCommonOptions(parser)
Options.addSEOptions(parser)

# #####################################################################

parser.add_argument("--enable-pim-ctrl", action="store_true",
                    help="Enable PIM MMIO controller")

# parser.add_argument("--pim-mmio-base", type=lambda x: int(x, 0),
#                     default=0x2F000000,
#                     help="Base address of PIM MMIO controller")
parser.add_argument("--pim-mmio-base", type=lambda x: int(x, 0),
                    default=0x100000000,
                    help="Base address of PIM MMIO controller")

parser.add_argument("--pim-se-va", type=lambda x: int(x, 0),
                    default=0xFFFF0000,
                    help="User virtual address mapped to PIM MMIO in SE mode")


####################################################################

if "--ruby" in sys.argv:
    Ruby.define_options(parser)

args = parser.parse_args()




multiprocesses = []
numThreads = 1

if args.bench:
    apps = args.bench.split("-")
    if len(apps) != args.num_cpus:
        print("number of benchmarks not equal to set num_cpus!")
        sys.exit(1)

    for app in apps:
        try:
            if ObjectList.cpu_list.get_isa(args.cpu_type) == ISA.ARM:
                exec(
                    "workload = %s('arm_%s', 'linux', '%s')"
                    % (app, args.arm_iset, args.spec_input)
                )
            else:
                # TARGET_ISA has been removed, but this is missing a ], so it
                # has incorrect syntax and wasn't being used anyway.
                exec(
                    "workload = %s(buildEnv['TARGET_ISA', 'linux', '%s')"
                    % (app, args.spec_input)
                )
            multiprocesses.append(workload.makeProcess())
        except:
            print(
                f"Unable to find workload for ISA: {app}",
                file=sys.stderr,
            )
            sys.exit(1)
elif args.cmd:
    multiprocesses, numThreads = get_processes(args)
else:
    print("No workload specified. Exiting!\n", file=sys.stderr)
    sys.exit(1)


(CPUClass, test_mem_mode, FutureClass) = Simulation.setCPUClass(args)
CPUClass.numThreads = numThreads

# Check -- do not allow SMT with multiple CPUs
if args.smt and args.num_cpus > 1:
    fatal("You cannot use SMT with multiple CPUs!")

np = args.num_cpus
mp0_path = multiprocesses[0].executable
system = System(
    cpu=[CPUClass(cpu_id=i) for i in range(np)],
    mem_mode=test_mem_mode,
    mem_ranges=[AddrRange(args.mem_size)],
    cache_line_size=args.cacheline_size,
)

# PIM MMIO mapping requires Process objects to be real children of the
# SimObject tree before process.map() is called. Otherwise gem5 reports
# an orphan workload / Parent.eventq_index error.
# if args.enable_pim_ctrl:
#     system.pim_processes = multiprocesses


if numThreads > 1:
    system.multi_thread = True

# Create a top-level voltage domain
system.voltage_domain = VoltageDomain(voltage=args.sys_voltage)

# Create a source clock for the system and set the clock period
system.clk_domain = SrcClockDomain(
    clock=args.sys_clock, voltage_domain=system.voltage_domain
)

# Create a CPU voltage domain
system.cpu_voltage_domain = VoltageDomain()

# Create a separate clock domain for the CPUs
system.cpu_clk_domain = SrcClockDomain(
    clock=args.cpu_clock, voltage_domain=system.cpu_voltage_domain
)

# If elastic tracing is enabled, then configure the cpu and attach the elastic
# trace probe
if args.elastic_trace_en:
    CpuConfig.config_etrace(CPUClass, system.cpu, args)

# All cpus belong to a common cpu_clk_domain, therefore running at a common
# frequency.
for cpu in system.cpu:
    cpu.clk_domain = system.cpu_clk_domain

if ObjectList.is_kvm_cpu(CPUClass) or ObjectList.is_kvm_cpu(FutureClass):
    if buildEnv["USE_X86_ISA"]:
        system.kvm_vm = KvmVM()
        system.m5ops_base = max(0xFFFF0000, Addr(args.mem_size).getValue())
        for process in multiprocesses:
            process.useArchPT = True
            process.kvmInSE = True
    else:
        fatal("KvmCPU can only be used in SE mode with x86")

# Sanity check
if args.simpoint_profile:
    if not ObjectList.is_noncaching_cpu(CPUClass):
        fatal("SimPoint/BPProbe should be done with an atomic cpu")
    if np > 1:
        fatal("SimPoint generation not supported with more than one CPUs")





for i in range(np):
    if args.smt:
        system.cpu[i].workload = multiprocesses
    elif len(multiprocesses) == 1:
        system.cpu[i].workload = multiprocesses[0]
    else:
        system.cpu[i].workload = multiprocesses[i]
    
    # ###################################################################################3
    
    # if args.smt:
    #     system.cpu[0].workload = multiprocesses

    #     if args.enable_pim_ctrl:
    #         for process in multiprocesses:
    #             process.map(args.pim_se_va, args.pim_mmio_base, 0x1000, False)

    # else:
    #     for i in range(np):
    #         system.cpu[i].workload = multiprocesses[i]

    #     if args.enable_pim_ctrl:
    #         for process in multiprocesses:
    #             process.map(args.pim_se_va, args.pim_mmio_base, 0x1000, False)
    
    ######################################################################################  
    

    if args.simpoint_profile:
        system.cpu[i].addSimPointProbe(args.simpoint_interval)

    if args.checker:
        system.cpu[i].addCheckerCpu()

    if args.bp_type:
        bpClass = ObjectList.bp_list.get(args.bp_type)
        system.cpu[i].branchPred = bpClass()

    if args.indirect_bp_type:
        indirectBPClass = ObjectList.indirect_bp_list.get(
            args.indirect_bp_type
        )
        system.cpu[i].branchPred.indirectBranchPred = indirectBPClass()

    system.cpu[i].createThreads()

if args.ruby:
    Ruby.create_system(args, False, system)
    assert args.num_cpus == len(system.ruby._cpu_ports)

    system.ruby.clk_domain = SrcClockDomain(
        clock=args.ruby_clock, voltage_domain=system.voltage_domain
    )
    for i in range(np):
        ruby_port = system.ruby._cpu_ports[i]

        # Create the interrupt controller and connect its ports to Ruby
        # Note that the interrupt controller is always present but only
        # in x86 does it have message ports that need to be connected
        system.cpu[i].createInterruptController()

        # Connect the cpu's cache ports to Ruby
        ruby_port.connectCpuPorts(system.cpu[i])
else:
    MemClass = Simulation.setMemClass(args)
    system.membus = SystemXBar()
    system.system_port = system.membus.cpu_side_ports
    CacheConfig.config_cache(args, system)
    MemConfig.config_mem(args, system)
    ####################################################
    # if args.enable_pim_ctrl:
    #     if not hasattr(system, "mem_ctrls") or len(system.mem_ctrls) == 0:
    #         fatal("PIMCtrl requires system.mem_ctrls, but no memory controller was created.")

    #     system.pim_ctrl = PIMCtrl(
    #         pio_addr=args.pim_mmio_base,
    #         pio_latency="10ns",
    #         pim_mem=system.mem_ctrls[0]
    # )

    #     system.pim_ctrl.pio = system.membus.mem_side_ports
    
    if args.enable_pim_ctrl:
        if args.mem_type != "PIMSim":
            fatal("When --enable-pim-ctrl is used, please also use --mem-type=PIMSim.")

        if not hasattr(system, "mem_ctrls") or len(system.mem_ctrls) == 0:
            fatal("PIMCtrl requires system.mem_ctrls, but no memory controller was created.")

        system.pim_ctrl = PIMCtrl(
            pio_addr=args.pim_mmio_base,
            pio_latency="10ns",
            pim_mem=system.mem_ctrls[0]
    )
        system.pim_ctrl.pio = system.membus.mem_side_ports
    ########################################################
    config_filesystem(system, args)

system.workload = SEWorkload.init_compatible(mp0_path)

 

 

if args.wait_gdb:
    system.workload.wait_for_remote_gdb = True

root = Root(full_system=False, system=system)

if args.enable_pim_ctrl:
    # Process.map() invokes the C++ Process object. In recent gem5 versions,
    # calling it before m5.instantiate() causes an orphan-workload
    # Parent.eventq_index error. Use Simulation.run only to instantiate the
    # SimObject tree, install the SE virtual->physical MMIO mapping, then run.
    _original_initialize_only = args.initialize_only
    args.initialize_only = True
    Simulation.run(args, root, system, FutureClass)

    for process in multiprocesses:
        process.map(args.pim_se_va, args.pim_mmio_base, 0x1000, False)

    if _original_initialize_only:
        sys.exit(0)

    _simulate_ticks = m5.MaxTick
    if args.abs_max_tick:
        _simulate_ticks = max(0, int(args.abs_max_tick) - m5.curTick())
    elif args.rel_max_tick:
        _simulate_ticks = int(args.rel_max_tick)
    elif args.maxtime:
        _simulate_ticks = m5.ticks.fromSeconds(args.maxtime)

    exit_event = m5.simulate(_simulate_ticks)
    print("Exiting @ tick %i because %s" % (m5.curTick(), exit_event.getCause()))
else:
    Simulation.run(args, root, system, FutureClass)