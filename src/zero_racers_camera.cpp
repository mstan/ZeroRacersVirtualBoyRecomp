#include "zero_racers_camera.h"
#include "zero_racers_bus.h"
#include <algorithm>

bool ZrCameraBuildStage(ZrCameraStage stage,ZrViewGeometry& view,const ZrWorldState& world,
                        const std::array<ZrActorState,6>& actors,unsigned slot,
                        const uint8_t* rom,size_t size,ZrNativeCall native) {
    if(!rom || size!=1048576 || slot>=actors.size() || world.course<1 || world.course>16)return false;
    auto next=view;ZrMappedBus bus(rom,size);
    // Records are strided A0 in the cartridge, not sizeof(ZrActorState).
    for(unsigned i=0;i<actors.size();++i)
        bus.constant(0x0500db70u+i*0xa0,actors[i].body.data(),actors[i].body.size());
    bus.constant(0x0500d270u,world.models.data(),world.models.size());
    bus.constant(0x05001058u,&world.course,sizeof(world.course));
    bus.map(0x050010acu,&next.camera.mode,sizeof(next.camera.mode));
    bus.map(0x050010b0u,next.camera.smoothing.data(),next.camera.smoothing.size());
    bus.map(0x050011f0u,next.camera.pose.data(),next.camera.pose.size());
    bus.map(0x050010c0u,&next.near_sector,sizeof(next.near_sector));
    bus.map(0x050010c4u,&next.far_sector,sizeof(next.far_sector));
    bus.map(0x050010c8u,&next.edges,sizeof(next.edges));
    bus.map(0x050010ccu,&next.vertices,sizeof(next.vertices));
    bus.map(0x05001208u,next.vertex.data(),next.vertex.size());
    bus.map(0x05004220u,next.edge.data(),next.edge.size());
    bus.map(0x05005230u,next.line.data(),next.line.size());
    CPUState cpu{};cpu.gpr[3]=0x0500fffcu;cpu.gpr[4]=0x05008000u;
    uint32_t entry=0,return_pc=0;
    switch(stage) {
    case ZrCameraStage::Pose:
        entry=0xfff0ef40u;return_pc=0xfff0f696u;
        cpu.gpr[6]=0x050011f0u;cpu.gpr[7]=0x050011fcu;cpu.gpr[8]=slot;break;
    case ZrCameraStage::Sector:
        entry=0xfff13d64u;return_pc=0xfff0f6fcu;cpu.gpr[6]=0x0500db70u+slot*0xa0;break;
    case ZrCameraStage::Models:entry=0xfff13f58u;return_pc=0xfff0f704u;break;
    case ZrCameraStage::Tunnel:entry=0xfff1436eu;return_pc=0xfff0f708u;break;
    case ZrCameraStage::Project:
        entry=0xfff14a3eu;return_pc=0xfff0f724u;
        cpu.gpr[6]=0x050011f0u;cpu.gpr[7]=0x050011fcu;cpu.gpr[8]=next.vertices;break;
    case ZrCameraStage::Clip:
        entry=0xfff14e16u;return_pc=0xfff0f730u;cpu.gpr[6]=next.edges;break;
    }
    const auto result=ZrRoutineRun(cpu,bus.callbacks(),entry,return_pc,2000000,native);
    if(!result.completed)return false;
    if(stage==ZrCameraStage::Clip)next.lines=cpu.gpr[10];
    if(next.edges>514 || next.vertices>1026 || next.lines>1026)return false;
    view=next;return true;
}
bool ZrRasterGeometry(const ZrViewGeometry& view,ZrEyePlanes& output,
                       const uint8_t* rom,size_t size,ZrNativeCall native) {
    if(!rom || size!=1048576 || view.lines>1026)return false;
    auto next=output;ZrMappedBus bus(rom,size);
    bus.map(0,next[0].data(),next[0].size());
    bus.map(0x10000,next[1].data(),next[1].size());
    bus.constant(0x05005230u,view.line.data(),view.line.size());
    const uint16_t bank=0;bus.constant(0x05001196u,&bank,sizeof(bank));
    CPUState cpu{};cpu.gpr[3]=0x0500fffcu;cpu.gpr[4]=0x05008000u;cpu.gpr[6]=view.lines;
    const auto result=ZrRoutineRun(cpu,bus.callbacks(),0xfff13ad2u,0xfff0fb9au,8000000,native);
    if(!result.completed)return false;
    output=next;return true;
}
bool ZrCameraBuild(ZrViewGeometry& view,const ZrWorldState& world,
                   const std::array<ZrActorState,6>& actors,unsigned slot,
                   const uint8_t* rom,size_t size,ZrNativeCall native) {
    auto next=view;
    for(auto stage:{ZrCameraStage::Pose,ZrCameraStage::Sector,ZrCameraStage::Models,
                    ZrCameraStage::Tunnel,ZrCameraStage::Project,ZrCameraStage::Clip})
        if(!ZrCameraBuildStage(stage,next,world,actors,slot,rom,size,native))return false;
    view=next;return true;
}
