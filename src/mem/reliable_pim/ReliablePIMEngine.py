from m5.params import *
from m5.SimObject import SimObject


class ReliablePIMEngine(SimObject):

    type = "ReliablePIMEngine"

    cxx_header = \
        "mem/reliable_pim/reliable_pim_engine.hh"

    cxx_class = \
        "gem5::reliable_pim::ReliablePIMEngine"

    enable = Param.Bool(
        False,
        "Enable reliable PIM")

    ecc_mode = Param.String(
        "CRC8",
        "NONE/CRC8/CRC16/SEC")

    codeword_flip = Param.Bool(
        True,
        "Enable Codeword Flip")

    correction_mode = Param.String(
        "HW",
        "HW or SW rank-level correction")

    vrt_rate = Param.Float(
        0.0,
        "Per-bit stationary VRT weak-cell rate")

    random_seed = Param.UInt64(
        1,
        "VRT fault-model seed")

    functional_ecc = Param.Bool(
        False,
        "Enable bit-accurate ECC mode")

    rank_correction_latency = Param.Latency(
        "63.75ns",
        "Temporary first-version correction latency")