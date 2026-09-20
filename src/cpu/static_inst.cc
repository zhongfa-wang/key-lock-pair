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

#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sys/types.h>

#include "base/trace.hh"
#include "base/types.hh"
#include "cpu/thread_context.hh"
// [klp] {
#include "cpu/addr_prov.hh"
#include "cpu/exec_context.hh"
#include "debug/KLPDEBUG.hh"
#include "debug/KLPPRINT.hh"
#include "cpu/base.hh"
#include "cpu/o3/dyn_inst.hh"
#include "cpu/reg_class.hh"
// } [klp]

namespace gem5
{

// [klp] {
// uint64_t 
// StaticInst::genSecTag(ExecContext *xc) const{
//   uint64_t tagVal=0x0, tmpTagVal=0x0, rsVal=0x0;
//   BaseCPU *cpu = xc->tcBase()->getCpuPtr();
//   const RegId& rs1_regId = this->srcRegIdx(0);
//   gem5::o3::DynInst *inst = dynamic_cast<gem5::o3::DynInst*>(xc);
//   bool baseRegUnknown = false;
  
//   if (rs1_regId.is(gem5::IntRegClass)) {
//     RegIndex rs1Idx = rs1_regId.index();
//     if(rs1Idx==2 || rs1Idx==3 || rs1Idx==4){
//       rsVal = xc->getRegOperand(this, 0);
//       tmpTagVal = std::invoke(cpu->hashingFuncPtr,cpu,rsVal,rsVal,cpu->getParaTagPos()) & cpu->getWidthMask();
//       inst->setIsBaseUnknown(gem5::triStateVal::FALSE);
//     } else {
//       if (!isOffsetZero()){
//         rsVal = xc->getRegOperand(this, 0);
//         tmpTagVal = std::invoke(cpu->hashingFuncPtr,cpu,rsVal,rsVal,cpu->getParaTagPos()) & cpu->getWidthMask();
//         inst->setIsBaseUnknown(gem5::triStateVal::FALSE);
//       } else {
//         inst->setIsBaseUnknown(gem5::triStateVal::TRUE);
//         baseRegUnknown = true;
//       }
//     }

//     /* if(inst->numSrcRegs() == 1){
//       if (inst->renamedSrcIdx(0)->testIsBase()) {
//         rsVal = inst->getRegOperand(this,0);
//         tmpTagVal = std::invoke(cpu->hashingFuncPtr,cpu,rsVal,rsVal,cpu->getParaTagPos()) & cpu->getWidthMask();
//       } else {
//         inst->setIsBaseUnknown(gem5::triStateVal::TRUE);
//         baseRegUnknown = true;
//       }
//     }
//     if(inst->numSrcRegs() == 2){
//       if( inst->renamedSrcIdx(0)->testIsBase() && !inst->renamedSrcIdx(1)->testIsBase()){
//         rsVal = inst->getRegOperand(this,0);
//         tmpTagVal = std::invoke(cpu->hashingFuncPtr,cpu,rsVal,rsVal,cpu->getParaTagPos()) & cpu->getWidthMask();
//       } else if(!inst->renamedSrcIdx(0)->testIsBase() &&  inst->renamedSrcIdx(1)->testIsBase()){
//         rsVal = inst->getRegOperand(this,0);
//         tmpTagVal = std::invoke(cpu->hashingFuncPtr,cpu,rsVal,rsVal,cpu->getParaTagPos()) & cpu->getWidthMask();
//       } else {
//         inst->setIsBaseUnknown(gem5::triStateVal::TRUE);
//         baseRegUnknown = true;
//       }

//     }
//     if(inst->numSrcRegs() > 2){
//       inst->setIsBaseUnknown(gem5::triStateVal::TRUE);
//       baseRegUnknown = true;
//     } */

//   }
//   if (!baseRegUnknown){
//     std::string tagGenSrc = cpu->getParaTagGenSrc();
//     /* MSB = 1 means it's a legal sec tag value. */
//     tagVal = tmpTagVal | 0x8000'0000'0000'0000;
//     inst->setIsBaseUnknown(gem5::triStateVal::FALSE);
//     DPRINTF(KLPPRINT, "[StaticInst] Generating the secure tag of the inst. Inst VA: 0x%x, inst assembly: %s, "
//                       "mask: 0x%x, secure tag with mask: 0x%x, parameter-tag pos: %d, "
//                       "parameter-tag granularity: %d.\n",
//                       inst->pcState().instAddr(),
//                       disassemble(inst->pcState().instAddr(),0),
//                       cpu->getWidthMask(),
//                       tagVal,
//                       cpu->getParaTagPos(),
//                       cpu->getParaTagGranularity());}
//   if(!inst->isUncondi()){
//     if(baseRegUnknown){
//       ++cpu->getBaseStats().numBaseUnKnown;
//       ++cpu->getBaseStats().numBaseSum;
//     } else {
//       ++cpu->getBaseStats().numBaseUnKnown;
//       ++cpu->getBaseStats().numBaseSum;
//     }
//   }
//   return tagVal;
// }
uint64_t
StaticInst::genSecTag(ExecContext *xc) const
{
    assert(xc != nullptr);
    assert(numSrcRegs() > 0);

    BaseCPU *cpu = xc->tcBase()->getCpuPtr();
    assert(cpu != nullptr);
    if (!cpu->isKlpEnabled()) {
        xc->setIsBaseUnknown(gem5::triStateVal::TRUE);
        return 0;
    }

    /*
     * 对于当前 RISC-V scalar/compressed load，
     * source operand 0 是 EA base register。
     *
     * getAddrProvOperand() 会通过 DynInst 的 renamedSrcIdx(0)
     * 读取对应 physical integer register 的 provenance。
     */
    const AddrProv prov = xc->getAddrProvOperand(this, 0);
    const CredentialDecision decision = decideCredential(prov);

    /*
     * genSecTag() 由 O3 的 LQ（load）或 EA code（store）调用。
     * DynInst 仅用于统计和调试，不参与 provenance 提取。
     */
    auto *inst = dynamic_cast<gem5::o3::DynInst *>(xc);
    assert(inst != nullptr);

    // The LQ generates a load key once per dynamic execution attempt.
    // Authorization can precede key generation, so uncondi loads count too.
    // Stores continue to generate their credential during address generation.
    if (!decision.allowSpeculation) {
        xc->setIsBaseUnknown(gem5::triStateVal::TRUE);

        // klpKeyGenerated prevents port/STLF retries from counting twice.
        if (inst->isKlpLoad() || !inst->isUncondi()) {
            ++cpu->getBaseStats().numBaseUnKnown;
            ++cpu->getBaseStats().numBaseSum;
        }

        DPRINTF(KLPDEBUG,
                "[StaticInst] Credential unavailable. "
                "Inst PC: %#lx, inst: %s, provenance state: %u.\n",
                inst->pcState().instAddr(),
                disassemble(inst->pcState().instAddr(), 0),
                static_cast<unsigned>(prov.state));

        /*
         * 0 只表示 packet 没有合法 security tag。
         * LQ 必须通过 isBaseUnknown 区分这种情况。
         */
        return 0;
    }

    xc->setIsBaseUnknown(gem5::triStateVal::FALSE);

    if (inst->isKlpLoad() || !inst->isUncondi()) {
        ++cpu->getBaseStats().numBaseKnown;
        ++cpu->getBaseStats().numBaseSum;
    }

    /*
     * credential 必须来自 AddrProv::candidate，不能重新读取
     * Rs1 的实际值。candidate 是最近双整数源运算选中的原始
     * operand，或 load 返回的指针值；单整数源运算原样传递它，
     * 因而它不一定等于最终 Rs1/rd 的值。
     */
    const RegVal credential = decision.credential;

    uint64_t tag =
        std::invoke(
            cpu->hashingFuncPtr,
            cpu,
            credential,
            credential,
            cpu->getParaTagPos()) &
        cpu->getWidthMask();

    /*
     * MSB 表示这是一个合法 security tag。
     *
     * credential 本身可以合法地等于 0，所以不能通过
     * credential == 0 判断 credential 是否存在。
     */
    constexpr uint64_t ValidTagBit = uint64_t{1} << 63;
    tag |= ValidTagBit;

    DPRINTF(KLPPRINT,
            "[StaticInst] Generated credential tag. "
            "Inst PC: %#lx, inst: %s, provenance state: %u, "
            "credential: %#llx, mask: %#llx, tag: %#llx, "
            "parameter-tag pos: %d, parameter-tag granularity: %d.\n",
            inst->pcState().instAddr(),
            disassemble(inst->pcState().instAddr(), nullptr),
            static_cast<unsigned>(prov.state),
            static_cast<unsigned long long>(credential),
            static_cast<unsigned long long>(cpu->getWidthMask()),
            static_cast<unsigned long long>(tag),
            cpu->getParaTagPos(),
            cpu->getParaTagGranularity());

    return tag;
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
