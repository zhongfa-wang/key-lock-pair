import argparse
import sys

import m5
from m5.objects import (
    NULL,
    BaseCache,
)

from gem5.components.boards.riscv_board import RiscvBoard
from gem5.components.cachehierarchies.classic.private_l1_private_l2_shared_l3_cache_hierarchy import *
from gem5.components.cachehierarchies.classic.private_l1_private_l2_walk_cache_hierarchy import (
    PrivateL1PrivateL2WalkCacheHierarchy,
)
from gem5.components.memory import SingleChannelDDR3_1600
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_switchable_processor import (
    SimpleSwitchableProcessor,
)
from gem5.isas import ISA
from gem5.resources.resource import (
    FileResource,
    obtain_resource,
)
from gem5.simulate.simulator import Simulator
from gem5.utils.requires import requires

# Setup an arg parser
parser = argparse.ArgumentParser(
    description="Argument parser for this RISCV gem5 configuration scritpt."
)
parser.add_argument(
    "--fwrdinsts",
    type=int,
    default=10000000,
    help="Number of instructions to fast forward using ATOMIC CPU.",
)
parser.add_argument(
    "--o3insts",
    type=int,
    default=None,
    help="The maximum number of instructions under simulation.",
)

parser.add_argument(
    "cmd",
    nargs=argparse.REMAINDER,
    help="Path to and arguments for the simualted binary.",
)
args = parser.parse_args()

# Run a check to ensure the right version of gem5 is being used.
requires(isa_required=ISA.RISCV)

# Setup the cache hierarchy.
# For classic, PrivateL1PrivateL2 and NoCache have been tested.
# For Ruby, MESI_Two_Level and MI_example have been tested.

cache_hierarchy = NoPrefetchP1P2S3CacheHierarchy(
    l1i_size="32KiB",
    l1i_assoc=8,
    l1d_size="32KiB",
    l1d_assoc=8,
    l2_size="512KiB",
    l2_assoc=8,
    l3_size="2MiB",
    l3_assoc=16,
)

# Setup the system memory.
memory = SingleChannelDDR3_1600()

# Setup a single core Processor.
processor = SimpleSwitchableProcessor(
    starting_core_type=CPUTypes.ATOMIC,
    switch_core_type=CPUTypes.O3,
    isa=ISA.RISCV,
    num_cores=1,
)

# Setup the board.
board = RiscvBoard(
    clk_freq="1GHz",
    processor=processor,
    memory=memory,
    cache_hierarchy=cache_hierarchy,
)

# Set the Syscall Emulation (SE) workload.
board.set_se_binary_workload(
    binary=FileResource(args.cmd[0]), arguments=args.cmd[1:]
)

simulator = Simulator(board=board)

if args.fwrdinsts > 0:
    print(
        f"Phase 1: Fast-forwarding {args.fwrdinsts} instructions using ATOMIC CPU..."
    )
    simulator.schedule_max_insts(args.fwrdinsts)
    simulator.run()

    print("Fast-forward complete. Switching to O3 CPU...")
    processor.switch()

    m5.stats.reset()

if args.o3insts > 0:
    print(f"Phase 2: Executing {args.o3insts} instructions using O3 CPU...")
    # 由于已经切换到 O3 核心，这个指令上限会针对新激活的 O3 核心重新计数
    simulator.schedule_max_insts(args.o3insts)
    simulator.run()

print("Simulation finished!")
