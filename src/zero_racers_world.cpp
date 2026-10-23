#include "zero_racers_world.h"
#include "zero_racers_bus.h"
#include <algorithm>

namespace {
constexpr uint32_t actors=0x0500db70u,models=0x0500d270u;
CPUState scratch(uint32_t actor,unsigned slot=0) {
    CPUState cpu{};cpu.gpr[3]=0x0500fffcu;cpu.gpr[4]=0x05008000u;
    cpu.gpr[6]=actor;cpu.gpr[7]=slot;return cpu;
}
}
bool ZrActorProgress(ZrActorState& actor,const ZrWorldState& world,const uint8_t* rom,
                     size_t size,bool& crossed,ZrNativeCall native) {
    if(!rom || size!=1048576 || world.course<1 || world.course>16)return false;
    auto candidate=actor;ZrMappedBus bus(rom,size);
    bus.map(actors,candidate.body.data(),candidate.body.size());
    bus.constant(0x05001058u,&world.course,sizeof(world.course));
    auto cpu=scratch(actors);
    auto result=ZrRoutineRun(cpu,bus.callbacks(),0xfff247b2u,0xfff0f598u,200000,native);
    if(!result.completed)return false;
    actor=candidate;crossed=cpu.gpr[10]!=0;return true;
}
bool ZrActorEnvironment(ZrEnvironment kind,ZrActorState& actor,ZrWorldState& world,
                        unsigned slot,const uint8_t* rom,size_t size,
                        ZrEnvironmentEvents& events,ZrNativeCall native) {
    if(!rom || size!=1048576 || slot>=6 || world.course<1 || world.course>16)return false;
    auto candidate=actor;auto candidate_world=world;ZrEnvironmentEvents commands{};
    ZrMappedBus bus(rom,size);
    const uint32_t pointer=actors+slot*0xa0u;
    bus.map(pointer,candidate.body.data(),candidate.body.size());
    bus.constant(0x05001058u,&world.course,sizeof(world.course));
    bus.constant(0x05001028u,&world.random,sizeof(world.random));
    bus.constant(0x05001100u,&slot,sizeof(slot));
    const uint32_t racing=0;bus.constant(0x050010f4u,&racing,sizeof(racing));
    bus.map(0x05001104u,&candidate.native_1104,sizeof(candidate.native_1104));
    bus.map(0x05001148u,&candidate.native_1148,sizeof(candidate.native_1148));
    bus.track(0x05001160u,&commands.native_1160,sizeof(commands.native_1160),commands.written,1);
    bus.track(0x050000e2u,&commands.native_e2,1,commands.written,2);
    bus.track(0x050000f1u,&commands.native_f1,1,commands.written,4);
    bus.track(0x050000f2u,&commands.native_f2,1,commands.written,8);
    // Native wall geometry uses these as call-local arithmetic temporaries.
    std::array<uint8_t,8> arithmetic{};bus.map(0x050007ecu,arithmetic.data(),arithmetic.size());
    if(kind==ZrEnvironment::Repair)
        bus.map(models+12*32,candidate.repair_models.data(),candidate.repair_models.size());
    else bus.map(models,candidate_world.models.data(),candidate_world.models.size());
    auto cpu=scratch(pointer,slot);
    uint32_t entry=0xfff1207eu,return_pc=0xfff0f62au;
    if(kind==ZrEnvironment::Surface){entry=0xfff12cccu;return_pc=0xfff0f612u;}
    if(kind==ZrEnvironment::Repair){entry=0xfff248eeu;return_pc=0xfff0f650u;}
    auto result=ZrRoutineRun(cpu,bus.callbacks(),entry,return_pc,200000,native);
    if(!result.completed)return false;
    actor=candidate;world=candidate_world;events=commands;return true;
}
bool ZrActorContact(ZrActorState& first,unsigned first_slot,ZrActorState& second,
                    unsigned second_slot,ZrWorldState& world,const uint8_t* rom,size_t size,
                    ZrEnvironmentEvents& events,ZrNativeCall native) {
    if(!rom || size!=1048576 || first_slot>=6 || second_slot>=6 || first_slot==second_slot
       || &first==&second || !world.native_10fc || world.native_10fc>=1785)return false;
    std::array<uint8_t,6*0xa0> pair{};
    std::copy(first.body.begin(),first.body.end(),pair.begin()+first_slot*0xa0);
    std::copy(second.body.begin(),second.body.end(),pair.begin()+second_slot*0xa0);
    // The stock response chooses human damage tables by model identity. All
    // versus racers are human, even when they picked different machines.
    // Normalize only this response's classification field, retaining profiles
    // and restoring the original render identities before committing.
    std::array<uint8_t,4> first_id{},second_id{};
    std::copy_n(first.body.begin()+0x30,4,first_id.begin());
    std::copy_n(second.body.begin()+0x30,4,second_id.begin());
    for(unsigned slot:{first_slot,second_slot})
        for(unsigned i=0;i<4;++i)pair[slot*0xa0+0x30+i]=uint8_t(world.native_10fc>>(8*i));
    auto candidate_world=world;ZrEnvironmentEvents commands{};ZrMappedBus bus(rom,size);
    bus.map(actors,pair.data(),pair.size());
    bus.map(models,candidate_world.models.data(),candidate_world.models.size());
    bus.constant(0x05001100u,&first_slot,sizeof(first_slot));
    bus.constant(0x050010fcu,&world.native_10fc,sizeof(world.native_10fc));
    bus.track(0x050000f1u,&commands.native_f1,1,commands.written,4);
    auto cpu=scratch(0);
    const auto result=ZrRoutineRun(cpu,bus.callbacks(),0xfff11c4au,0xfff0f5e0u,200000,native);
    if(!result.completed)return false;
    std::copy(first_id.begin(),first_id.end(),pair.begin()+first_slot*0xa0+0x30);
    std::copy(second_id.begin(),second_id.end(),pair.begin()+second_slot*0xa0+0x30);
    std::copy_n(pair.begin()+first_slot*0xa0,0xa0,first.body.begin());
    std::copy_n(pair.begin()+second_slot*0xa0,0xa0,second.body.begin());
    world=candidate_world;events=commands;return true;
}
