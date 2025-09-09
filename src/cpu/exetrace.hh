/*
 * Copyright (c) 2023 Arm Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2001-2005 The Regents of The University of Michigan
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

#ifndef __CPU_EXETRACE_HH__
#define __CPU_EXETRACE_HH__

// [klp] { Begin
#include <iostream>
#include "sim/core.hh"
#include "sim/sim_exit.hh"
// [klp] } End
#include "base/trace.hh"
#include "base/types.hh"
#include "cpu/static_inst.hh"
#include "cpu/thread_context.hh"
#include "debug/ExecEnable.hh"
#include "params/ExeTracer.hh"
#include "sim/insttracer.hh"

namespace gem5
{

class ThreadContext;

namespace trace {

class ExeTracerRecord : public InstRecord
{
  // [klp] { The item strucutre tracking inst info
  public:  
    struct rs1_info             // info of each rs1
      {
        Addr inst_vaddr;        // the addr of the inst that rs1 resides in,
        long long mem_acc_freq; // the frequency in which the inst accesses the 16B maddr
        rs1_info():inst_vaddr(0),mem_acc_freq(0){}
      };
    struct maddr_info                         // info of each maddr
    {
      long long switch_freq;                  // frequency of instructions using new rs1 to access the 16B maddr
      RegVal last_rs1_val;                    // previous rs1's value that accesses the 16B maddr
      std::map<RegVal,rs1_info> rs1_data_map; // rs1:rs1_info pair
      maddr_info():switch_freq(0),last_rs1_val(0),rs1_data_map(){}
    };
    // static std::map<Addr, std::map<RegVal,rs1_info>> suite_mem_acc_tracking; //Deprecated
    static std::map<Addr, maddr_info> suite_mem_acc_tracking;
  // [klp] } End
  public:
    ExeTracerRecord(Tick _when, ThreadContext *_thread,
               const StaticInstPtr _staticInst, const PCStateBase &_pc,
               const ExeTracer &_tracer,
               const StaticInstPtr _macroStaticInst = NULL)
        : InstRecord(_when, _thread, _staticInst, _pc, _macroStaticInst),
          tracer(_tracer)
    {
        vectorLengthInBytes = _thread->getIsaPtr()->getVectorLengthInBytes();
    }

    void traceInst(const StaticInstPtr &inst, bool ran);

    void dump();

  protected:
    const ExeTracer &tracer;
    int64_t vectorLengthInBytes;
};

class ExeTracer : public InstTracer
{
    // [klp] { 
  private:
    static void dumpStats(){
      std::cout << "Dumping statistics in dumpStats() function.\n";
      std::ofstream outfile("./MemAccessStats.json");
      if (!outfile.is_open()){
        std::cout<<"Error: could not onpen the logging file.\n" << std::endl;
        return;
      }
      outfile << "{\n";
      for(auto it_maddr = ExeTracerRecord::suite_mem_acc_tracking.begin();
          // {
          //   $maddr: {
          //     "rs1_switch_freq" :,
          //     "rs1_info":[
          //       {$rs1:$rs1_freq_val, "instVaddr": $instvaddr},
          //       ...]
          //   }
          //    ...
          // }
          it_maddr != ExeTracerRecord::suite_mem_acc_tracking.end(); it_maddr++){
            outfile << " \"0x" << std::hex << it_maddr->first << "\": {\n";
            outfile << "  \"rs1_switch_freq\": " << std::dec << it_maddr->second.switch_freq << ",\n";
            for(auto it_rs1 = it_maddr->second.rs1_data_map.begin();it_rs1 != it_maddr->second.rs1_data_map.end(); it_rs1++){
              outfile << "  \"0x"<< std::hex << it_rs1->first << "\": {\n";
              // outfile << "    \"instVaddr\": \"0x" << std::hex << it_rs1->second.rs1_data_map.inst_vaddr << "\",\n";
              outfile << "    \"freq\": " << std::dec << it_rs1->second.mem_acc_freq << "\n";
              outfile << "  }";
              if(it_rs1 != --it_maddr->second.rs1_data_map.end()){
                outfile << ",\n";
              }else{
                outfile << "\n";
              }
            }
            outfile << " }";
            if(it_maddr != --ExeTracerRecord::suite_mem_acc_tracking.end()){
              outfile << ",\n";
            }else{
              outfile << "\n";
            }
          }
      outfile << "}\n";
      outfile.close();
    }
  // [klp] } End
  public:
    typedef ExeTracerParams Params;
    ExeTracer(const Params &params) : InstTracer(params)
    {
      std::cout << "Creating an ExeTracer object.\n";
      std::cout << "Registring a callback dump function.\n";
      registerExitCallback(dumpStats);
    }

    InstRecord *
    getInstRecord(Tick when, ThreadContext *tc,
            const StaticInstPtr staticInst, const PCStateBase &pc,
            const StaticInstPtr macroStaticInst=nullptr) override
    {
        if (!debug::ExecEnable)
            return NULL;

        return new ExeTracerRecord(when, tc,
                staticInst, pc, *this, macroStaticInst);
    }
};

} // namespace trace
} // namespace gem5

#endif // __CPU_EXETRACE_HH__
