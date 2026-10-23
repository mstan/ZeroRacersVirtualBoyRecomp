#include "zero_racers_routine.h"
#include "v810_interpreter.h"
#include <limits>

namespace {
struct Active {
    CPUState* cpu;
    const ZrRoutineBus* bus;
    bool failed;
    uint32_t pc, address;
};
thread_local Active* active;
void fail(uint32_t address) {
    if(!active->failed) {
        active->failed=true;active->pc=active->cpu->pc;active->address=address;
    }
    active->cpu->cycle_deadline=active->cpu->cycles;
}
uint32_t read(uint32_t address,unsigned width) {
    uint32_t value=0;
    if(!active->failed && !active->bus->read(active->bus->context,address,width,&value))fail(address);
    return value;
}
void write(uint32_t address,unsigned width,uint32_t value) {
    if(!active->failed && !active->bus->write(active->bus->context,address,width,value))fail(address);
}
uint8_t read8(uint32_t a){return (uint8_t)read(a,1);}
uint16_t read16(uint32_t a){return (uint16_t)read(a,2);}
uint32_t read32(uint32_t a){return read(a,4);}
void write8(uint32_t a,uint8_t v){write(a,1,v);}
void write16(uint32_t a,uint16_t v){write(a,2,v);}
void write32(uint32_t a,uint32_t v){write(a,4,v);}
struct ExecutionGuard {
    VbExecutionStats saved=vb_execution;
    ExecutionGuard(){vb_execution={};vb_execution.mode=VB_EXEC_HYBRID;}
    ~ExecutionGuard(){vb_execution=saved;}
};
struct BusGuard {
    CPUState& cpu;
    ~BusGuard() {
        active=nullptr;
        cpu.read8=nullptr;cpu.read16=nullptr;cpu.read32=nullptr;
        cpu.write8=nullptr;cpu.write16=nullptr;cpu.write32=nullptr;
    }
};
}
ZrRoutineResult ZrRoutineRun(CPUState& cpu,const ZrRoutineBus& bus,uint32_t entry,
                            uint32_t return_pc,uint64_t cycle_limit,ZrNativeCall native) {
    if(active || !bus.read || !bus.write || (entry&1) || (return_pc&1)
       || entry==return_pc || !cycle_limit
       || cycle_limit>std::numeric_limits<uint64_t>::max()-cpu.cycles)
        return {false,entry,0,0};
    ExecutionGuard guard;
    Active call{&cpu,&bus,false,0,0};active=&call;
    cpu.read8=read8;cpu.read16=read16;cpu.read32=read32;
    cpu.write8=write8;cpu.write16=write16;cpu.write32=write32;
    BusGuard scoped_bus{cpu};
    const uint64_t begin=cpu.cycles;
    cpu.pc=entry;cpu.gpr[31]=return_pc;cpu.halted=cpu.yielded=0;
    cpu.cycle_deadline=begin+cycle_limit;cpu.step_budget=cycle_limit;
    if(native)native(&cpu,entry,return_pc);
    else {
        while(cpu.pc!=return_pc && !cpu.halted && !call.failed && cpu.cycles<cpu.cycle_deadline)
            vb_interpreter_step(&cpu);
    }
    const bool completed=!call.failed && !cpu.halted && cpu.pc==return_pc
        && cpu.cycles-begin<=cycle_limit;
    return {completed,call.failed?call.pc:cpu.pc,call.address,cpu.cycles-begin};
}
