#include "zero_racers_warning.h"
#include "zero_racers_bus.h"

bool ZrWarningValidate(const ZrWarningState& s) {
    auto bias=[](uint32_t n){return n==0 || n==80 || n==uint32_t(-80);};
    return s.previous_damage<=800 && bias(s.model_bias) && bias(s.display_bias);
}
bool ZrActorWarning(ZrWarningState& state,const ZrActorState& actor,uint32_t frame,
                     const uint8_t* rom,size_t size,ZrEnvironmentEvents& events,ZrNativeCall native) {
    if(!rom || size!=1048576)return false;
    auto next=state;next.display_bias=state.model_bias;
    ZrEnvironmentEvents commands{};ZrMappedBus bus(rom,size);
    bus.constant(0x0500db70,actor.body.data(),actor.body.size());
    bus.constant_word(0x05001048,frame);
    bus.word(0x05001168,next.previous_damage);bus.word(0x0500114c,next.model_bias);
    bus.track_word(0x05001160,commands.native_1160,commands.written,1);
    bus.map(0x0003db00,next.power_world.data(),next.power_world.size());
    CPUState cpu{};cpu.gpr[3]=0x0500fffc;cpu.gpr[4]=0x05008000;cpu.gpr[6]=0x0500db70;
    const auto result=ZrRoutineRun(cpu,bus.callbacks(),0xfff18c18,0xfff0fb8e,20000,native);
    if(!result.completed || !ZrWarningValidate(next))return false;
    state=next;events=commands;return true;
}
