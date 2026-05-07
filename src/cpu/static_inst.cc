/*
 * Copyright (c) 2003-2005 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "cpu/static_inst.hh"

#include <cstdint>
#include <iostream>

#include "base/trace.hh"
#include "cpu/thread_context.hh"
// [klp] {
#include "cpu/exec_context.hh"
#include "debug/KLPDEBUG.hh"
// } [klp]

namespace gem5
{

// [klp] {
/* Hashing. Currently using xor by bit as the hashing function. */
uint64_t 
StaticInst::hashing(uint64_t val1, uint64_t val2) const{
  return (val1 ^ val2);
}

/* Generate security tag return a uint32_t value */
uint64_t 
StaticInst::genSecTagFramePC(ExecContext *xc, uint64_t spRegVal) const{
  // uint64_t tagVal = 0x0;
  uint64_t tagVal;
  BaseCPU *cpu = xc->tcBase()->getCpuPtr();
  Addr pc = xc->pcState().instAddr();
  std::string tagGenSrc = cpu->getParaTagGenSrc();
  tagVal = hashing(spRegVal,pc) & cpu->getWidthMask(); // Set the instruction tag
  /* MSB = 1 means it's a legal sec tag value. */
  tagVal |= 0x8000'0000'0000'0000;
  DPRINTF(KLPDEBUG, "[StaticInst] Generating the secure tag of the inst. Inst VA: 0x%x, inst assembly: %s, sp reg val: 0x%x, mask: 0x%x, secure tag value: 0x%x.\n",
                    pc,
                    disassemble(pc,0),
                    spRegVal,
                    cpu->getWidthMask(),
                    tagVal);
  return tagVal;
}
// } [klp]
StaticInstPtr
StaticInst::fetchMicroop(MicroPC upc) const
{
    panic("StaticInst::fetchMicroop() called on instruction "
          "that is not microcoded.");
}

std::unique_ptr<PCStateBase>
StaticInst::branchTarget(const PCStateBase &pc) const
{
    panic("StaticInst::branchTarget() called on instruction "
          "that is not a PC-relative branch.");
}

std::unique_ptr<PCStateBase>
StaticInst::branchTarget(ThreadContext *tc) const
{
    panic("StaticInst::branchTarget() called on instruction "
          "that is not an indirect branch.");
}

const std::string &
StaticInst::disassemble(Addr pc, const loader::SymbolTable *symtab) const
{
    if (!cachedDisassembly) {
        cachedDisassembly =
            std::make_unique<std::string>(generateDisassembly(pc, symtab));
    }

    return *cachedDisassembly;
}

void
StaticInst::printFlags(std::ostream &outs,
    const std::string &separator) const
{
    bool printed_a_flag = false;

    for (unsigned int flag = IsNop; flag < Num_Flags; flag++) {
        if (flags[flag]) {
            if (printed_a_flag)
                outs << separator;

            outs << FlagsStrings[flag];
            printed_a_flag = true;
        }
    }
}

void
StaticInst::advancePC(ThreadContext *tc) const
{
    std::unique_ptr<PCStateBase> pc(tc->pcState().clone());
    advancePC(*pc);
    tc->pcState(*pc);
}

} // namespace gem5
