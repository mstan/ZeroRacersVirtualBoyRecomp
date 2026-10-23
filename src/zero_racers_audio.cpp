#include "zero_racers_audio.h"
#include "zero_racers_bus.h"
#include <cstdio>
bool ZrActorNearbyAudio(ZrActorState& actor,const std::array<ZrActorState,6>& actors,unsigned slot,
                        const ZrCameraState& camera,const uint8_t* rom,size_t size,
                        ZrNearbyAudio& audio,ZrEnvironmentEvents& events,ZrNativeCall native) {
    if(!rom || size!=1048576 || slot>=actors.size())return false;
    ZrMappedBus bus(rom,size);uint32_t pass=actor.native_1104;
    ZrNearbyAudio next{};ZrEnvironmentEvents commands{};
    for(unsigned i=0;i<actors.size();++i)
        bus.constant(0x0500db70+i*0xa0,actors[i].body.data(),actors[i].body.size());
    bus.constant(0x050011f0,camera.pose.data(),camera.pose.size());
    bus.constant_word(0x05001100,slot);bus.word(0x05001104,pass);
    bus.word(0x050007f4,next.speed);bus.word(0x050007f8,next.direction);bus.word(0x050007fc,next.distance);
    bus.track(0x050000f2,&commands.native_f2,1,commands.written,8);
    CPUState cpu{};cpu.gpr[3]=0x0500fffc;cpu.gpr[4]=0x05008000;
    const auto result=ZrRoutineRun(cpu,bus.callbacks(),0xfff0ed56,0xfff0fa7e,200000,native);
    if(!result.completed) {
        std::fprintf(stderr,"Nearby audio refused pc=%08x address=%08x\n",result.failure_pc,result.failure_address);return false;
    }
    actor.native_1104=pass;audio=next;events=commands;return true;
}
