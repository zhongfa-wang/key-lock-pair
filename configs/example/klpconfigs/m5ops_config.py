import sys
import argparse
from gem5.components.boards.riscv_board import RiscvBoard
from gem5.components.cachehierarchies.classic.private_l1_private_l2_walk_cache_hierarchy import (
    PrivateL1PrivateL2WalkCacheHierarchy,
)
from gem5.components.cachehierarchies.classic.private_l1_private_l2_shared_l3_cache_hierarchy import *
from gem5.components.memory import SingleChannelDDR3_1600
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import obtain_resource
from gem5.simulate.simulator import Simulator
from gem5.utils.requires import requires
from gem5.resources.resource import FileResource
# [klp] {
from m5.objects import BaseCache, NULL
# } [klp]

# Setup an arg parser
parser = argparse.ArgumentParser(
    description="Argument parser for this RISCV gem5 configuration scritpt."
)
parser.add_argument(
    "--maxinsts",
    type    = int,
    default = None,
    help    = "The maximum number of instructions under simulation."
)
# [klp] {
parser.add_argument(
    "--tag_width",
    type    = int,
    default = 4,
    help    = "The width of the key."
)
parser.add_argument(
    "--tag_pos",
    type    = int,
    default = 4, 
    help    = "Tag position. Controlling on which bit from LSB of the hashing result register starts the tag. "
)
parser.add_argument(
    "--tag_granularity",
    type    = int,
    default = 16,
    help    = "The granularity of keys/locks. One key per 16 Bytes by default. The blkSize should be divisible by this number."
)
parser.add_argument(
    "--threat_model",
    type    = str,
    default = "spectre",
    help    = "Threat model. Options: 'spectre' (default), 'futuristic', 'disable'." # Disable means sec tag verification always return true.
)
parser.add_argument(
    "--tag_gen_src",
    type    = str,
    default = "framePc",
    help    = "The source information to generate the key. Options: 'framePc' by default, 'baseAddr'. "
)
# } [klp]
parser.add_argument(
    "--cpu",
    type    = str,
    choices = ["atomic", "o3"],
    default = "o3",
    help    = "Select the CPU model. Use 'atomic' for fast-forwarding, 'o3' for detailed simulation."
)
parser.add_argument(
    "--prog_interval",
    type    = int,
    default = 1000,
)
parser.add_argument(
    "cmd",
    nargs = argparse.REMAINDER,
    help  = "Path to and arguments for the simualted binary."
)
args = parser.parse_args()

# Run a check to ensure the right version of gem5 is being used.
requires(isa_required=ISA.RISCV)

# Setup the cache hierarchy.
# For classic, PrivateL1PrivateL2 and NoCache have been tested.
# For Ruby, MESI_Two_Level and MI_example have been tested.
            
cache_hierarchy = NoPrefetchP1P2S3CacheHierarchy(
        l1i_size  = "32KiB",
        l1i_assoc = 8,
        l1d_size  = "32KiB",
        l1d_assoc = 8,
        l2_size   = "512KiB",
        l2_assoc  = 8,
        l3_size   = "2MiB",
        l3_assoc  = 16,
        # [klp] {
        tag_width       = args.tag_width,
        tag_pos         = args.tag_pos,
        tag_granularity = args.tag_granularity,
        threat_model    = args.threat_model
        # } [klp]
        )

# Setup the system memory.
memory = SingleChannelDDR3_1600()
# checkpoint {
selected_cpu_type = CPUTypes.ATOMIC if args.cpu == "atomic" else CPUTypes.O3
# } checkpoint
processor = SimpleProcessor(
    cpu_type  = selected_cpu_type, #O3, ATOMIC 
    isa       = ISA.RISCV, 
    num_cores = 1
)

# Setup the board.
board = RiscvBoard(
    clk_freq        = "1GHz",
    processor       = processor,
    memory          = memory,
    cache_hierarchy = cache_hierarchy,
)
# [klp] {
# Pass parameters to BaseCPU
for core in processor.get_cores():
    core.core.tag_width       = args.tag_width
    core.core.tag_pos         = args.tag_pos
    core.core.threat_model    = args.threat_model
    core.core.tag_gen_src     = args.tag_gen_src
    core.core.tag_granularity = args.tag_granularity
    # if args.prog_interval > 0:
    #     core.core.progress_interval = str(args.prog_interval)
# } [klp]

# Set the Syscall Emulation (SE) workload.
board.set_se_binary_workload(
    binary    = FileResource(args.cmd[0]),
    arguments = args.cmd[1:]
)

# M5 ops handler
from gem5.simulate.exit_event import ExitEvent
def exit_on_checkpoint():
    yield True

simulator = Simulator(
    board=board,
    on_exit_event={
        ExitEvent.CHECKPOINT: exit_on_checkpoint()
    }
)
# simulator = Simulator(board=board)

if args.maxinsts:
    print(f"Scheduling simulation exit after {args.maxinsts} instructions.")
    simulator.schedule_max_insts(args.maxinsts)


print("Beginning simulation!")
simulator.run()
print("Simulation finished!")
