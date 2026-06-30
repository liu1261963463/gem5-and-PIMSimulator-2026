from m5.params import *
from m5.objects.AbstractMemory import *

class PIMSim(AbstractMemory):
    type = "PIMSim"
    cxx_header = "mem/pimsim.hh"
    cxx_class = "gem5::memory::PIMSim"

    port = ResponsePort("PIMSim memory port")

    deviceConfigFile = Param.String(
        "ini/HBM2_samsung_2M_16B_x64.ini",
        "PIMSimulator device configuration file"
    )

    systemConfigFile = Param.String(
        "system_hbm.ini",
        "PIMSimulator system configuration file"
    )

    filePath = Param.String(
        "ext/pimsim/PIMSimulator",
        "Path to PIMSimulator root"
    )

    traceFile = Param.String("", "PIMSimulator trace output file")

    enableDebug = Param.Bool(False, "Enable PIMSimulator debug output")

    pimCycle = Param.Latency(
        "1ns",
        "One PIMSimulator cycle translated to gem5 ticks"
    )

    pimChannels = Param.Unsigned(
        16,
        "Number of PIM pseudo channels used by PIMKernel"
    )

    pimRanks = Param.Unsigned(
        1,
        "Number of PIM ranks used by PIMKernel"
    )