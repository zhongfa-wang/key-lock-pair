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
    help    = "The threat model. Options: 'spectre' by default, 'futuristic'. "
)
parser.add_argument(
    "--tag_gen_src",
    type    = str,
    default = "framePc",
    help    = "The source information to generate the key. Options: 'framePc' by default, 'baseAddr'. "
)
# } [klp]
# checkpoint {
parser.add_argument(
    "--save_cpt",
    type    = str,
    default = None,
    help    = "Path to save the checkpoint (e.g., 'cpt_dir/')."
)
parser.add_argument(
    "--restore_cpt",
    type    = str,
    default = None,
    help    = "Path to restore from a checkpoint."
)
parser.add_argument(
    "--cpu",
    type    = str,
    choices = ["atomic", "o3"],
    default = "o3",
    help    = "Select the CPU model. Use 'atomic' for fast-forwarding, 'o3' for detailed simulation."
)
# } checkpoint
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

# cache_hierarchy = PrivateL1PrivateL2WalkCacheHierarchy(
#     l1d_size="32KiB", l1i_size="32KiB", l2_size="512KiB"
# )
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
        tag_granularity = args.tag_granularity
        # } [klp]
        )

# Setup the system memory.
memory = SingleChannelDDR3_1600()

# Setup a single core Processor.
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
if args.cpu != "atomic":
  for core in processor.get_cores():
      core.core.tag_width       = args.tag_width
      core.core.tag_pos         = args.tag_pos
      core.core.threat_model    = args.threat_model
      core.core.tag_gen_src     = args.tag_gen_src
      core.core.tag_granularity = args.tag_granularity
# } [klp]

# Set the Syscall Emulation (SE) workload.
board.set_se_binary_workload(
    binary    = FileResource(args.cmd[0]),
    arguments = args.cmd[1:]
)
# checkpoint {
if args.restore_cpt:
    print(f"Restoring simulation from checkpoint: {args.restore_cpt}")
    simulator = Simulator(board=board, checkpoint_path=args.restore_cpt)
else:
    simulator = Simulator(board=board)
# } checkpoint

# checkpoint {
print("Beginning simulation!")

# 如果指定了 max_insts，并且当前不是从快照恢复状态，则跑到指定 Insts num 停下
if args.maxinsts and not args.restore_cpt:
    print(f"Simulation will run until absolute insts: {args.maxinsts}")
    # simulator.run(max_ticks=args.max_ticks)
    simulator.schedule_max_insts(args.maxinsts)
    simulator.run()
else:
    # 如果没有指定 max_insts，或者正在从快照恢复，则一直运行到程序结束
    simulator.run()

# 模拟器停下来后，检查是否需要保存快照
if args.save_cpt:
    print(f"Reached target inst number or exit event. Saving checkpoint to {args.save_cpt}...")
    simulator.save_checkpoint(args.save_cpt)

print("Simulation finished!")
# } checkpoint
