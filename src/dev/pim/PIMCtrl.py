from m5.objects.Device import BasicPioDevice
from m5.params import *
from m5.proxy import *
from m5.objects.PIMSim import PIMSim

class PIMCtrl(BasicPioDevice):
    type = "PIMCtrl"
    cxx_header = "dev/pim/pim_ctrl.hh"
    cxx_class = "gem5::PIMCtrl"

    pio_size = Param.Addr(0x1000, "PIM control register space size")

    pim_mem = Param.PIMSim(
        "The PIMSim memory object controlled by this MMIO device"
    )