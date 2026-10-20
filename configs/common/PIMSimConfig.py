# 在PIMSimConfig.py文件顶部，全局标记
_added_pim_options = False
from m5.objects import *
from m5.util import fatal, warn


def _parse_int(value):
    if isinstance(value, int):
        return value
    return int(value, 0)


def addPIMSimOptions(parser):
    global _added_pim_options
    if _added_pim_options:
        return
    _added_pim_options = True
    parser.add_argument(
        "--use-pimsim",
        action="store_true",
        default=False,
        help="Use PIMSimulator-backed PIMSim as the main memory model."
    )      
    

    parser.add_argument(
        "--pimsim-device-config",
        type=str,
        default="ini/HBM2_samsung_2M_16B_x64.ini",
        help="PIMSimulator device configuration file relative to --pimsim-root.",
    )

    parser.add_argument(
        "--pimsim-system-config",
        type=str,
        default="system_hbm.ini",
        help="PIMSimulator system configuration file relative to --pimsim-root.",
    )

    parser.add_argument(
        "--pimsim-root",
        type=str,
        default="ext/pimsim/PIMSimulator",
        help="Path to the PIMSimulator root directory.",
    )

    parser.add_argument(
        "--pimsim-trace-file",
        type=str,
        default="",
        help="PIMSimulator trace output file. Empty string disables trace output.",
    )

    parser.add_argument(
        "--pimsim-debug",
        action="store_true",
        default=False,
        help="Enable PIMSimulator debug output.",
    )

    parser.add_argument(
        "--pimsim-cycle",
        type=str,
        default="1ns",
        help="Translate one PIMSimulator cycle to this gem5 latency.",
    )

    parser.add_argument(
        "--pimsim-channels",
        type=int,
        default=16,
        help="Number of PIM pseudo channels used by PIMKernel.",
    )

    parser.add_argument(
        "--pimsim-ranks",
        type=int,
        default=1,
        help="Number of PIM ranks used by PIMKernel.",
    )

    parser.add_argument(
        "--pimctrl",
        action="store_true",
        default=False,
        help="Create MMIO PIMCtrl device for launching PIM commands.",
    )

    parser.add_argument(
        "--pimctrl-addr",
        type=str,
        default="0x1000000000",
        help=(
            "Physical base address of the PIMCtrl MMIO register block. "
            "Use an address outside system.mem_ranges."
        ),
    )
    
    parser.add_argument(
        "--reliable-pim",
        action="store_true",
        default=False,
        help="Enable reliable PIM engine"
    )

    parser.add_argument(
        "--pim-ecc-mode",
        type=str,
        default="CRC8",
        choices=[
            "NONE",
            "CRC8",
            "CRC16",
            "SEC"
        ],
        help="Reliable PIM ECC mode"
    )

    parser.add_argument(
        "--codeword-flip",
        action="store_true",
        default=False,
        help="Enable Codeword Flip"
    )

    parser.add_argument(
        "--correction-mode",
        type=str,
        default="HW",
        choices=["HW", "SW"],
        help="Rank-level correction mode"
    )

    parser.add_argument(
        "--vrt-rate",
        type=float,
        default=0.0,
        help="VRT weak-cell rate"
    )
    


def configPIMSim(system, args, full_system=False):
    if not args.use_pimsim:
        return False

    if getattr(args, "ruby", False):
        fatal(
            "PIMSim integration only supports the classic memory system. "
            "Please do not use --ruby together with --use-pimsim."
        )

    if not hasattr(system, "membus"):
        fatal("configPIMSim() requires system.membus to exist.")

    if not hasattr(system, "mem_ranges") or len(system.mem_ranges) == 0:
        fatal("configPIMSim() requires system.mem_ranges to be configured.")

    if len(system.mem_ranges) != 1:
        fatal(
            "This first PIMSim integration only supports one memory range. "
            "Current system.mem_ranges has %d ranges." % len(system.mem_ranges)
        )

    system.pimsim = PIMSim(
        range=system.mem_ranges[0],
        deviceConfigFile=args.pimsim_device_config,
        systemConfigFile=args.pimsim_system_config,
        filePath=args.pimsim_root,
        traceFile=args.pimsim_trace_file,
        enableDebug=args.pimsim_debug,
        pimCycle=args.pimsim_cycle,
        pimChannels=args.pimsim_channels,
        pimRanks=args.pimsim_ranks,
    )
    

   

    # Keep a mem_ctrls-style alias for scripts or stats code that expect it.
    system.mem_ctrls = [system.pimsim]

    warn("Using PIMSim as the main memory controller.")

    if args.pimctrl:
        pio_addr = _parse_int(args.pimctrl_addr)

        system.pim_ctrl = PIMCtrl(
            pio_addr=pio_addr,
            pim_mem=system.pimsim,
        )
        
        # pimsim.reliable_pim = m5.objects.ReliablePIMEngine(
        # enable=options.reliable_pim,

        # ecc_mode=
        #     options.pim_ecc_mode,

        # codeword_flip=
        #     options.codeword_flip,

        # correction_mode=
        #     options.correction_mode,

        # vrt_rate=
        #     options.vrt_rate,

        # random_seed=1,

        # functional_ecc=False,

        # rank_correction_latency=
        #     "63.75ns",
        # )

        # For initial debugging, attach PIMCtrl directly to the memory bus.
        # This is simpler than routing through iobus/bridge in FS mode.
        system.pim_ctrl.pio = system.membus.mem_side_ports

        warn(
            "PIMCtrl MMIO device is enabled at physical address %#x. "
            "Please make sure this address does not overlap with RAM."
            % pio_addr
        )

    return True