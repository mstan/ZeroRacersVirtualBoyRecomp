#include "zero_racers_camera.h"
#include "zero_racers_bus.h"
#include <algorithm>
#include <memory>

namespace {
void observe_geometry(const CPUState* cpu,uint32_t address,unsigned width,uint32_t,void* context) {
    auto& view=*static_cast<ZrViewGeometry*>(context);const unsigned pc=cpu->pc&0xfffff;
    const unsigned off=address&65535;
    if(off>=0x4220 && off<0x5230 && !((off-0x4220)&7) && width==2) {
        if(pc==0x1403c) {
            const auto descriptor=cpu->gpr[6]&0xfffff;
            view.edge_tags[(off-0x4220)/8]=descriptor>=0x61080 && descriptor<0x68010 && !(descriptor&15)
                ?0x10000+(descriptor-0x61080)/16:0;
        } else if(pc>=0x1420e && pc<0x14960)view.edge_tags[(off-0x4220)/8]=1;
        // Clipping may edit these endpoints later. Retain their origin.
    }
    if(width==4 && (pc==0x15012 || pc==0x15150) && off>=0x5230 && !((off-0x5230)&31)
       && (off-0x5230)/32<view.line_tags.size()) {
        const unsigned edge=cpu->gpr[25]-0x05004220u;
        view.line_tags[(off-0x5230)/32]=edge<view.edge_tags.size()*8 && !(edge&7)?view.edge_tags[edge/8]:0;
    }
}
struct RasterObserver {const ZrViewGeometry& view;const ZrEyePlanes& planes;ZrEyeTags* tags;};
void observe_raster(const CPUState* cpu,uint32_t address,unsigned width,uint32_t value,void* context) {
    auto& c=*static_cast<RasterObserver*>(context);const unsigned eye=address>=0x10000;
    const unsigned off=address-eye*0x10000;
    if(off>=0x6000 || (cpu->pc&0xfffff)<0x13b5a || (cpu->pc&0xfffff)>0x13ccc)return;
    const unsigned line=cpu->gpr[17]-0x05005230u;
    const uint32_t tag=line<c.view.line_tags.size()*32 && !(line&31)?c.view.line_tags[line/32]:0;
    for(unsigned byte=0;byte<width;++byte)for(unsigned pixel=0;pixel<4;++pixel) {
        const unsigned x=(off+byte)/64,y=((off+byte)%64)*4+pixel;
        const unsigned before=(c.planes[eye][off+byte]>>(pixel*2))&3;
        const unsigned after=(value>>(byte*8+pixel*2))&3;
        if(y<224 && before!=after)(*c.tags)[eye][y*384+x]=after?tag:0;
    }
}
}

bool ZrCameraUpdatePose(ZrCameraState& camera,const std::array<ZrActorState,6>& actors,
                        unsigned slot,const uint8_t* rom,size_t size,ZrNativeCall native) {
    if(!rom || size!=1048576 || slot>=actors.size())return false;
    auto next=camera;ZrMappedBus bus(rom,size);
    for(unsigned i=0;i<actors.size();++i)
        bus.constant(0x0500db70u+i*0xa0,actors[i].body.data(),actors[i].body.size());
    bus.constant_word(0x050010acu,next.mode);
    bus.map(0x050010b0u,next.smoothing.data(),next.smoothing.size());
    bus.map(0x050011f0u,next.pose.data(),next.pose.size());
    CPUState cpu{};cpu.gpr[3]=0x0500fffcu;cpu.gpr[4]=0x05008000u;
    cpu.gpr[6]=0x050011f0u;cpu.gpr[7]=0x050011fcu;cpu.gpr[8]=slot;
    const auto result=ZrRoutineRun(cpu,bus.callbacks(),0xfff0ef40u,0xfff0f696u,200000,native);
    if(!result.completed)return false;
    camera=next;return true;
}
bool ZrCameraPrepareWorld(ZrWorldState& output,const ZrWorldState& world,uint32_t near_sector,
                          uint32_t far_sector,const uint8_t* rom,size_t size,ZrNativeCall native) {
    if(!rom || size!=1048576 || world.course<1 || world.course>16)return false;
    auto next=world;ZrMappedBus bus(rom,size);
    bus.map(0x0500d270u,next.models.data(),next.models.size());
    bus.constant_word(0x05001058u,world.course);
    bus.constant_word(0x050010c0u,near_sector);
    bus.constant_word(0x050010c4u,far_sector);
    CPUState cpu{};cpu.gpr[3]=0x0500fffcu;cpu.gpr[4]=0x05008000u;
    const auto result=ZrRoutineRun(cpu,bus.callbacks(),0xfff24aa2u,0xfff0f700u,200000,native);
    if(!result.completed)return false;
    output=next;return true;
}

bool ZrCameraBuildStage(ZrCameraStage stage,ZrViewGeometry& view,const ZrWorldState& world,
                        const std::array<ZrActorState,6>& actors,unsigned slot,
                        const uint8_t* rom,size_t size,ZrNativeCall native) {
    if(!rom || size!=1048576 || slot>=actors.size() || world.course<1 || world.course>16)return false;
    if(stage==ZrCameraStage::Pose)return ZrCameraUpdatePose(view.camera,actors,slot,rom,size,native);
    auto next=view;ZrMappedBus bus(rom,size);
    // Records are strided A0 in the cartridge, not sizeof(ZrActorState).
    for(unsigned i=0;i<actors.size();++i)
        bus.constant(0x0500db70u+i*0xa0,actors[i].body.data(),actors[i].body.size());
    bus.constant(0x0500d270u,world.models.data(),world.models.size());
    bus.constant_word(0x05001058u,world.course);
    bus.word(0x050010acu,next.camera.mode);
    bus.map(0x050010b0u,next.camera.smoothing.data(),next.camera.smoothing.size());
    bus.map(0x050011f0u,next.camera.pose.data(),next.camera.pose.size());
    bus.word(0x050010c0u,next.near_sector);
    bus.word(0x050010c4u,next.far_sector);
    bus.word(0x050010c8u,next.edges);
    bus.word(0x050010ccu,next.vertices);
    bus.map(0x05001208u,next.vertex.data(),next.vertex.size());
    bus.map(0x05004220u,next.edge.data(),next.edge.size());
    bus.map(0x05005230u,next.line.data(),next.line.size());
    CPUState cpu{};cpu.gpr[3]=0x0500fffcu;cpu.gpr[4]=0x05008000u;
    bus.observe(&cpu,observe_geometry,&next);
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
                       const uint8_t* rom,size_t size,ZrNativeCall native,ZrEyeTags* tags) {
    if(!rom || size!=1048576 || view.lines>1026)return false;
    auto next=output;ZrMappedBus bus(rom,size);
    bus.map(0,next[0].data(),next[0].size());
    bus.map(0x10000,next[1].data(),next[1].size());
    bus.constant(0x05005230u,view.line.data(),view.line.size());
    const uint16_t bank=0;bus.constant(0x05001196u,&bank,sizeof(bank));
    CPUState cpu{};cpu.gpr[3]=0x0500fffcu;cpu.gpr[4]=0x05008000u;cpu.gpr[6]=view.lines;
    std::unique_ptr<ZrEyeTags> next_tags;
    if(tags)next_tags=std::make_unique<ZrEyeTags>(*tags);
    // The optional metadata is view-owned and follows original first-writer
    // stores, including unchanged occluding pixels and separate eye planes.
    RasterObserver observer{view,next,next_tags.get()};
    if(tags)bus.observe(&cpu,observe_raster,&observer);
    const auto result=ZrRoutineRun(cpu,bus.callbacks(),0xfff13ad2u,0xfff0fb9au,8000000,native);
    if(!result.completed)return false;
    output=next;if(tags)*tags=*next_tags;return true;
}
bool ZrCameraBuild(ZrViewGeometry& view,const ZrWorldState& world,
                   const std::array<ZrActorState,6>& actors,unsigned slot,
                   const uint8_t* rom,size_t size,ZrNativeCall native) {
    auto camera=view.camera;
    return ZrCameraUpdatePose(camera,actors,slot,rom,size,native)
        && ZrCameraBuildFromPose(view,camera,world,actors,slot,rom,size,native);
}
bool ZrCameraBuildFromPose(ZrViewGeometry& view,const ZrCameraState& camera,
                           const ZrWorldState& world,const std::array<ZrActorState,6>& actors,
                           unsigned slot,const uint8_t* rom,size_t size,ZrNativeCall native) {
    ZrViewGeometry next{};next.camera=camera;
    if(!ZrCameraBuildStage(ZrCameraStage::Sector,next,world,actors,slot,rom,size,native))return false;
    ZrWorldState prepared;
    if(!ZrCameraPrepareWorld(prepared,world,next.near_sector,next.far_sector,rom,size,native))return false;
    for(auto stage:{ZrCameraStage::Models,ZrCameraStage::Tunnel,
                    ZrCameraStage::Project,ZrCameraStage::Clip})
        if(!ZrCameraBuildStage(stage,next,prepared,actors,slot,rom,size,native))return false;
    view=next;return true;
}
