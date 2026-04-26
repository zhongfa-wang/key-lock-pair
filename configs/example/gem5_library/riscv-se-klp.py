# Copyright (c) 2021 The Regents of the University of California
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

"""
This example runs a simple linux boot. It uses the 'riscv-disk-img' resource.
It is built with the sources in `src/riscv-fs` in [gem5 resources](
https://github.com/gem5/gem5-resources).

Characteristics
---------------

* Runs exclusively on the RISC-V ISA with the classic caches
* Assumes that the kernel is compiled into the bootloader
* Automatically generates the DTB file
* Will boot but requires a user to login using `m5term` (username: `root`,
  password: `root`)
"""
import sys
import argparse
from gem5.components.boards.riscv_board import RiscvBoard
from gem5.components.cachehierarchies.classic.private_l1_private_l2_walk_cache_hierarchy import (
    PrivateL1PrivateL2WalkCacheHierarchy,
)
from gem5.components.cachehierarchies.classic.private_l1_private_l2_shared_l3_cache_hierarchy import(
    PrivateL1PrivateL2SharedL3CacheHierarchy,)
from gem5.components.memory import SingleChannelDDR3_1600
from gem5.components.processors.cpu_types import CPUTypes
from gem5.components.processors.simple_processor import SimpleProcessor
from gem5.isas import ISA
from gem5.resources.resource import obtain_resource
from gem5.simulate.simulator import Simulator
from gem5.utils.requires import requires
from gem5.resources.resource import FileResource
# [klp] {
from m5.objects import BaseCache
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
    help    = "The threat model. Options: 'spectre' by default, 'futuristic'. "
)
parser.add_argument(
    "--tag_gen_src",
    type    = str,
    default = "framePc",
    help    = "The source information to generate the key. Options: 'framePc' by default, 'baseAddr'. "
)
# } [klp]
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
cache_hierarchy = PrivateL1PrivateL2SharedL3CacheHierarchy(
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
# [klp] {
# Pass parameters to BaseCPU
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

simulator = Simulator(board=board)

if args.maxinsts:
    print(f"Scheduling simulation exit after {args.maxinsts} instructions.")
    simulator.schedule_max_insts(args.maxinsts)

print("Beginning simulation!")
simulator.run()
print("Simulation finished!")
