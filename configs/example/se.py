# Copyright (c) 2023 The Regents of the University of California
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

from m5.objects import *
from m5.util import fatal
from m5.objects import PIMSim


fatal(
    "The 'configs/example/se.py' script has been deprecated. It can be "
    "found in 'configs/deprecated/example' if required. Its usage should be "
    "avoided as it will be removed in future releases of gem5."
)

system.mem_ctrl = PIMSim(
    range = system.mem_ranges[0],
    deviceConfigFile = "ini/HBM2_samsung_2M_16B_x64.ini",
    systemConfigFile = "system_hbm.ini",
    filePath = "ext/pimsim/PIMSimulator",
    traceFile = "",
    enableDebug = False,
    pimCycle = "1ns",
    pimChannels = 16,
    pimRanks = 1
)

system.membus.mem_side_ports = system.mem_ctrl.port

system.pim_ctrl = PIMCtrl(
    pio_addr = 0x2F000000,
    pim_mem = system.mem_ctrl
)

if hasattr(system, "iobus"):
    system.pim_ctrl.pio = system.iobus.mem_side_ports
else:
    system.pim_ctrl.pio = system.membus.mem_side_ports