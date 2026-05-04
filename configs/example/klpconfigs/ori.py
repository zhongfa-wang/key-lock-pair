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
from m5.objects import BaseCache, NULL  

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
        )
        
# Setup the system memory.
memory = SingleChannelDDR3_1600()

# Setup a single core Processor.
processor = SimpleProcessor(
    cpu_type  = CPUTypes.O3, #O3, ATOMIC 
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

# Set the Syscall Emulation (SE) workload.
board.set_se_binary_workload(
    binary    = FileResource(args.cmd[0]),
    arguments = args.cmd[1:]
)

simulator = Simulator(board=board)

if args.maxinsts:
    print(f"Scheduling simulation exit after {args.maxinsts} instructions.")
    simulator.schedule_max_insts(args.maxinsts)

print("Beginning simulation!")
simulator.run()
print("Simulation finished!")
