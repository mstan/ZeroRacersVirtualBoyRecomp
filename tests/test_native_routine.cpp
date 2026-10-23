#include "zero_racers_routine.h"
#include "zero_racers_actor.h"
#include "zero_racers_world.h"
#include "zero_racers_camera.h"
#include "zero_racers_session.h"
#include "zero_racers_font.h"
#include "zero_racers_bus.h"
#include "v810_interpreter.h"
#include "vb_crc32.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <vector>

extern "C" void vb_stub_abort(const char* reason,uint32_t pc,uint32_t address) {
    std::cout<<"Native routine fault: "<<reason<<" pc="<<std::hex<<pc<<" addr="<<address<<'\n';
    std::abort();
}
extern "C" void vb_stub_abort_simple(const char* reason,uint32_t pc){vb_stub_abort(reason,pc,0);}
extern "C" void vb_fntrace_record(uint32_t,uint32_t){}
namespace {
std::vector<uint8_t> file(const char* name) {
    std::ifstream input(std::filesystem::u8path(name),std::ios::binary);
    if(!input)throw std::runtime_error("Cannot open fixture");
    return {std::istreambuf_iterator<char>(input),{}};
}
uint32_t u32(const uint8_t* p){return p[0]|(uint32_t(p[1])<<8)|(uint32_t(p[2])<<16)|(uint32_t(p[3])<<24);}
CPUState cpu(const std::vector<uint8_t>& blob) {
    if(blob.size()==16+offsetof(CPUState,read8) && !std::memcmp(blob.data(),"ZRROUT01",8)
       && u32(blob.data()+8)==offsetof(CPUState,read8)
       && u32(blob.data()+12)==vb_crc32_compute(blob.data()+16,blob.size()-16)) {
        CPUState c{};std::memcpy(&c,blob.data()+16,offsetof(CPUState,read8));return c;
    }
    if(blob.size()<112+offsetof(CPUState,read8) || std::memcmp(blob.data(),"VBSTATE1",8)
       || u32(blob.data()+8)!=blob.size()
       || u32(blob.data()+80)!=vb_crc32_compute(blob.data()+112,blob.size()-112))
        throw std::runtime_error("Invalid private native snapshot fixture");
    CPUState c{};std::memcpy(&c,blob.data()+112,offsetof(CPUState,read8));return c;
}
struct Fixture {
    std::vector<uint8_t> rom,wram,video;
    std::map<uint32_t,unsigned> reads,writes;
    static bool read(void* context,uint32_t address,unsigned width,uint32_t* result) {
        auto& f=*static_cast<Fixture*>(context);address&=0x07ffffffu;
        const auto* data=address>>24==7 ? &f.rom:address>>24==5 ? &f.wram:
            (address<0x40000 && !f.video.empty())?&f.video:nullptr;
        if(!data || data->empty())return false;
        *result=0;
        for(unsigned i=0;i<width;++i)*result|=uint32_t((*data)[(address+i)&(data->size()-1)])<<(i*8);
        if(address>>24==5)++f.reads[address];return true;
    }
    static bool write(void* context,uint32_t address,unsigned width,uint32_t value) {
        auto& f=*static_cast<Fixture*>(context);address&=0x07ffffffu;
        if(address<0x40000 && !f.video.empty()) {
            if(address+width>f.video.size())return false;
            for(unsigned i=0;i<width;++i)f.video[address+i]=uint8_t(value>>(i*8));
            return true;
        }
        if(address>>24!=5)return false;
        for(unsigned i=0;i<width;++i)f.wram[(address+i)&65535]=(uint8_t)(value>>(i*8));
        ++f.writes[address];return true;
    }
};
}
int main(int argc,char** argv) {
    if(argc==2 && !std::strcmp(argv[1],"--layout")) {
        std::cout<<"{\"cpu_prefix_bytes\":"<<offsetof(CPUState,read8)<<"}\n";return 0;
    }
    if(argc!=6 && argc!=8){std::cout<<"ROM before.state before-wram.bin after.state after-wram.bin [before-vram.bin after-vram.bin]\n";return 2;}
    try {
        {
            ZrMappedBus bus(nullptr,0);uint32_t scalar=0x12345678,dirty=0,value=0;
            if(!bus.track_word(0x05001000,scalar,dirty,4) || !bus.constant_word(0x05001010,0x87654321))
                throw std::runtime_error("Numeric guest-word mapping failed");
            auto memory=bus.callbacks();
            if(!memory.read(memory.context,0x05001001,2,&value) || value!=0x3456
               || !memory.read(memory.context,0x05001013,1,&value) || value!=0x87
               || !memory.write(memory.context,0x05001001,2,0xabcd) || scalar!=0x12abcd78 || dirty!=4
               || memory.write(memory.context,0x05001010,4,0))
                throw std::runtime_error("Numeric word byte order/partial store/RO contract failed");
        }
        Fixture f{file(argv[1]),file(argv[3]),argc==8?file(argv[6]):std::vector<uint8_t>{},{},{}},original=f;
        if(f.rom.size()!=1048576 || vb_crc32_compute(f.rom.data(),f.rom.size())!=0x71553796
           || f.wram.size()!=65536)throw std::runtime_error("Wrong stock cartridge/WRAM fixture");
        CPUState c=cpu(file(argv[2])),expected=cpu(file(argv[4]));
        vb_execution.native_entries=123;vb_execution.breakpoint_enabled=1;
        vb_execution.breakpoint_pc=c.pc;vb_execution.stepping=1;vb_execution.stopped=1;
        const auto saved=vb_execution;const auto before=c;
        const auto result=ZrRoutineRun(c,{&f,Fixture::read,Fixture::write},c.pc,expected.pc,1000000);
        if(!result.completed)throw std::runtime_error("Routine did not return within its bounded bus contract");
        bool matched=true;
        for(unsigned i=0;i<32;++i)if(c.gpr[i]!=expected.gpr[i]){
            std::cout<<"GPR mismatch "<<i<<" actual="<<std::hex<<c.gpr[i]<<" expected="<<expected.gpr[i]<<'\n';matched=false;
        }
        if(c.pc!=expected.pc || vb_psw_pack(&c)!=vb_psw_pack(&expected) || c.cycles!=expected.cycles
           || c.pipeline_class!=expected.pipeline_class || c.bus_tail_cycles!=expected.bus_tail_cycles)
            {std::cout<<"CPU exit mismatch cycles="<<std::dec<<c.cycles<<" expected="<<expected.cycles<<'\n';matched=false;}
        auto end=file(argv[5]);if(f.wram!=end) {
            for(size_t i=0;i<f.wram.size();++i)if(i>=end.size() || f.wram[i]!=end[i]) {
                std::cout<<"WRAM mismatch offset="<<std::hex<<i<<'\n';break;
            }
            matched=false;
        }
        if(std::memcmp(&saved,&vb_execution,sizeof(saved)))throw std::runtime_error("Live debugger/execution control changed");
        if(before.pc==0xfff151c4u) {
            const unsigned offset=before.gpr[6]&65535u;
            if(offset+0xa0>original.wram.size())throw std::runtime_error("Bad native actor pointer");
            ZrActorState actor{};
            std::copy_n(original.wram.begin()+offset,actor.body.size(),actor.body.begin());
            actor.native_1140=u32(original.wram.data()+0x1140);
            actor.native_1144=u32(original.wram.data()+0x1144);
            const auto input=u32(original.wram.data()+0x1024);
            actor.previous_buttons=(uint16_t)(input&~(input>>16));
            ZrActorEvents events;
            const bool stepped=ZrActorHumanStep(actor,original.rom.data(),original.rom.size(),(uint16_t)input,
                {u32(original.wram.data()+0x106c),u32(original.wram.data()+0x1070)},events);
            if(!stepped || !std::equal(actor.body.begin(),actor.body.end(),end.begin()+offset)
               || actor.native_1140!=u32(end.data()+0x1140) || actor.native_1144!=u32(end.data()+0x1144))
                throw std::runtime_error("Classified actor adapter differs from native mechanics");
            for(auto pair: {std::pair<uint32_t,uint32_t>{0x1160,events.native_1160},{0x116c,events.native_116c},
                    {0xee,events.native_ee},{0xf1,events.native_f1},{0xf2,events.native_f2},{0xf3,events.native_f3}})
                if(f.writes.count(0x05000000u+pair.first) && pair.second!=
                   (pair.first<0x100?end[pair.first]:u32(end.data()+pair.first)))
                    throw std::runtime_error("Human command differs from native output");
            std::cout<<"Classified actor-only bus matches native result\n";
        }
        if(before.pc==0xfff247b2u || before.pc==0xfff1207eu || before.pc==0xfff12cccu || before.pc==0xfff248eeu) {
            const unsigned offset=before.gpr[6]&65535u,slot=(offset-0xdb70u)/0xa0;
            if(slot>=6 || offset!=0xdb70+slot*0xa0)throw std::runtime_error("Invalid actor slot");
            ZrActorState actor{};ZrWorldState world{};
            std::copy_n(original.wram.begin()+offset,actor.body.size(),actor.body.begin());
            std::copy_n(original.wram.begin()+0xd270,world.models.size(),world.models.begin());
            std::copy_n(original.wram.begin()+0xd3f0,actor.repair_models.size(),actor.repair_models.begin());
            actor.native_1104=u32(original.wram.data()+0x1104);
            actor.native_1148=u32(original.wram.data()+0x1148);
            world.course=u32(original.wram.data()+0x1058);world.random=u32(original.wram.data()+0x1028);
            bool stepped=false,crossed=false;ZrEnvironmentEvents events{};
            if(before.pc==0xfff247b2u) {
                stepped=ZrActorProgress(actor,world,original.rom.data(),original.rom.size(),crossed);
                if(crossed!=(expected.gpr[10]!=0))throw std::runtime_error("Progress crossing differs");
            } else {
                auto kind=before.pc==0xfff1207eu?ZrEnvironment::Wall:
                    before.pc==0xfff12cccu?ZrEnvironment::Surface:ZrEnvironment::Repair;
                stepped=ZrActorEnvironment(kind,actor,world,slot,original.rom.data(),original.rom.size(),events);
                if(actor.native_1104!=u32(end.data()+0x1104) || actor.native_1148!=u32(end.data()+0x1148))
                    throw std::runtime_error("Personal environment latches differ");
                if(kind==ZrEnvironment::Repair) {
                    if(!std::equal(actor.repair_models.begin(),actor.repair_models.end(),end.begin()+0xd3f0))
                        throw std::runtime_error("Personal repair beams differ");
                } else if(!std::equal(world.models.begin(),world.models.end(),end.begin()+0xd270))
                    throw std::runtime_error("Shared environment models differ");
                for(auto pair: {std::pair<uint32_t,uint32_t>{0x1160,events.native_1160},
                        {0xe2,events.native_e2},{0xf1,events.native_f1},{0xf2,events.native_f2}})
                    if(f.writes.count(0x05000000u+pair.first) && pair.second!=
                       (pair.first<0x100?end[pair.first]:u32(end.data()+pair.first)))
                        throw std::runtime_error("Environment command differs from native output");
            }
            if(!stepped || !std::equal(actor.body.begin(),actor.body.end(),end.begin()+offset))
                throw std::runtime_error("Classified environment adapter differs from native mechanics");
            std::cout<<"Classified environment bus matches native result\n";
        }
        if(before.pc==0xfff11c4au) {
            std::vector<unsigned> slots;std::array<ZrActorState,6> actors{};ZrWorldState world{};
            for(unsigned i=0;i<6;++i) {
                std::copy_n(original.wram.begin()+0xdb70+i*0xa0,0xa0,actors[i].body.begin());
                if(u32(actors[i].body.data()+0x30))slots.push_back(i);
            }
            if(slots.size()==2) {
                const unsigned selected=u32(original.wram.data()+0x1100);
                if(slots[1]==selected)std::swap(slots[0],slots[1]);
                if(slots[0]!=selected)throw std::runtime_error("Contact fixture has no selected human");
                std::copy_n(original.wram.begin()+0xd270,world.models.size(),world.models.begin());
                world.native_10fc=u32(original.wram.data()+0x10fc);
                ZrEnvironmentEvents events{};
                if(!ZrActorContact(actors[slots[0]],slots[0],actors[slots[1]],slots[1],world,
                                  original.rom.data(),original.rom.size(),events))
                    throw std::runtime_error("Classified contact bus refused the pair");
                for(unsigned slot:slots)if(!std::equal(actors[slot].body.begin(),actors[slot].body.end(),end.begin()+0xdb70+slot*0xa0))
                    throw std::runtime_error("Pair response differs from native result");
                if(!std::equal(world.models.begin(),world.models.end(),end.begin()+0xd270)
                   || (f.writes.count(0x050000f1u) && events.native_f1!=end[0xf1]))
                    throw std::runtime_error("Pair effects differ from native result");
                std::cout<<"Classified two-human swept contact matches native result\n";
                // A different render model must retain the same human damage
                // response. The stock reference uses its selected-human tag
                // for both records; the adapter virtualizes only that tag.
                for(auto pair: {std::pair<unsigned,uint32_t>{slots[0],263},{slots[1],257}}) {
                    const auto slot=pair.first;
                    std::copy_n(original.wram.begin()+0xdb70+slot*0xa0,0xa0,actors[slot].body.begin());
                    for(unsigned j=0;j<4;++j)actors[slot].body[0x30+j]=uint8_t(pair.second>>(8*j));
                }
                std::copy_n(original.wram.begin()+0xd270,world.models.size(),world.models.begin());
                if(!ZrActorContact(actors[slots[0]],slots[0],actors[slots[1]],slots[1],world,
                    original.rom.data(),original.rom.size(),events))throw std::runtime_error("Mixed-machine human contact refused");
                for(unsigned slot:slots)for(unsigned off=0;off<0xa0;++off)
                    if((off<0x30 || off>=0x34) && actors[slot].body[off]!=end[0xdb70+slot*0xa0+off])
                        throw std::runtime_error("Mixed-machine response used CPU damage rules");
                if(u32(actors[slots[0]].body.data()+0x30)!=263 || u32(actors[slots[1]].body.data()+0x30)!=257)
                    throw std::runtime_error("Contact replaced chosen machine identity");
            }
        }
        if(before.pc==0xfff100b6u) {
            ZrSessionState session;
            if(!ZrSessionInitialize(session,u32(original.wram.data()+0x1058),{0,1,2,3},4,
                u32(original.wram.data()+0x1028),original.rom.data(),original.rom.size()))
                throw std::runtime_error("Native-derived grid initialization refused");
            for(unsigned i=0;i<6;++i)for(unsigned off=0;off<0xa0;++off)
                if(session.actors[i].body[off]!=end[0xdb70+i*0xa0+off]) {
                    const unsigned selected=u32(original.wram.data()+0x1100);
                    if(i<4 && i!=selected && ((off>=0x58 && off<0x5c)||(off>=0x64 && off<0x68)))continue;
                    std::cout<<"Grid mismatch slot="<<i<<" offset="<<std::hex<<off<<" actual="
                        <<unsigned(session.actors[i].body[off])<<" native="<<unsigned(end[0xdb70+i*0xa0+off])<<'\n';
                    throw std::runtime_error("Initial actor differs from native grid/profile tables");
                }
            for(unsigned off=0;off<session.world.models.size();++off)if(session.world.models[off]!=end[0xd270+off]) {
                std::cout<<"Model mismatch offset="<<std::hex<<off<<" actual="<<unsigned(session.world.models[off])
                    <<" native="<<unsigned(end[0xd270+off])<<'\n';
                throw std::runtime_error("Initial course models differ from native setup");
            }
            std::cout<<"Native course/grid/profile initialization matches stock result\n";
        }
        if(before.pc==0xfff24aa2u) {
            ZrWorldState world{},prepared;
            world.course=u32(original.wram.data()+0x1058);
            std::copy_n(original.wram.begin()+0xd270,world.models.size(),world.models.begin());
            const auto immutable=world;
            if(!ZrCameraPrepareWorld(prepared,world,u32(original.wram.data()+0x10c0),u32(original.wram.data()+0x10c4),
                original.rom.data(),original.rom.size()) || !std::equal(prepared.models.begin(),prepared.models.end(),end.begin()+0xd270))
                throw std::runtime_error("View-dependent display-list preparation differs from native result");
            if(std::memcmp(&immutable,&world,sizeof(world)))throw std::runtime_error("View modified shared model state");
            std::cout<<"View-owned tunnel caps/display-list preparation matches native result\n";
        }
        if(before.pc==0xfff0ef40u || before.pc==0xfff13d64u || before.pc==0xfff13f58u
           || before.pc==0xfff1436eu || before.pc==0xfff14a3eu || before.pc==0xfff14e16u) {
            ZrWorldState world{};std::array<ZrActorState,6> actors{};ZrViewGeometry view{};
            const auto& source=original.wram;
            world.course=u32(source.data()+0x1058);
            std::copy_n(source.begin()+0xd270,world.models.size(),world.models.begin());
            for(unsigned i=0;i<6;++i)std::copy_n(source.begin()+0xdb70+i*0xa0,0xa0,actors[i].body.begin());
            view.camera.mode=u32(source.data()+0x10ac);
            std::copy_n(source.begin()+0x10b0,view.camera.smoothing.size(),view.camera.smoothing.begin());
            std::copy_n(source.begin()+0x11f0,view.camera.pose.size(),view.camera.pose.begin());
            view.near_sector=u32(source.data()+0x10c0);view.far_sector=u32(source.data()+0x10c4);
            view.edges=u32(source.data()+0x10c8);view.vertices=u32(source.data()+0x10cc);
            std::copy_n(source.begin()+0x1208,view.vertex.size(),view.vertex.begin());
            std::copy_n(source.begin()+0x4220,view.edge.size(),view.edge.begin());
            std::copy_n(source.begin()+0x5230,view.line.size(),view.line.begin());
            const auto before_state=file(argv[2]),after_state=file(argv[4]);
            const size_t tag_bytes=sizeof(view.edge_tags)+sizeof(view.line_tags);
            if(before_state.size()<tag_bytes || after_state.size()!=before_state.size())
                throw std::runtime_error("Camera metadata fixture missing");
            std::memcpy(view.edge_tags.data(),before_state.data()+before_state.size()-tag_bytes,sizeof(view.edge_tags));
            std::memcpy(view.line_tags.data(),before_state.data()+before_state.size()-sizeof(view.line_tags),sizeof(view.line_tags));
            auto stage=before.pc==0xfff0ef40u?ZrCameraStage::Pose:before.pc==0xfff13d64u?ZrCameraStage::Sector:
                before.pc==0xfff13f58u?ZrCameraStage::Models:before.pc==0xfff1436eu?ZrCameraStage::Tunnel:
                before.pc==0xfff14a3eu?ZrCameraStage::Project:ZrCameraStage::Clip;
            const auto world_before=world;const auto actors_before=actors;
            if(!ZrCameraBuildStage(stage,view,world,actors,u32(source.data()+0x1100),original.rom.data(),original.rom.size()))
                throw std::runtime_error("Classified camera bus refused the native stage");
            if(view.camera.mode!=u32(end.data()+0x10ac)
               || !std::equal(view.camera.smoothing.begin(),view.camera.smoothing.end(),end.begin()+0x10b0)
               || !std::equal(view.camera.pose.begin(),view.camera.pose.end(),end.begin()+0x11f0)
               || view.near_sector!=u32(end.data()+0x10c0) || view.far_sector!=u32(end.data()+0x10c4)
               || view.edges!=u32(end.data()+0x10c8) || view.vertices!=u32(end.data()+0x10cc)
               || !std::equal(view.vertex.begin(),view.vertex.end(),end.begin()+0x1208)
               || !std::equal(view.edge.begin(),view.edge.end(),end.begin()+0x4220)
               || !std::equal(view.line.begin(),view.line.end(),end.begin()+0x5230)
               || (stage==ZrCameraStage::Clip && view.lines!=expected.gpr[10]))
                throw std::runtime_error("Classified camera geometry differs from stock result");
            if(std::memcmp(view.edge_tags.data(),after_state.data()+after_state.size()-tag_bytes,sizeof(view.edge_tags))
               || std::memcmp(view.line_tags.data(),after_state.data()+after_state.size()-sizeof(view.line_tags),sizeof(view.line_tags)))
                throw std::runtime_error("View-owned geometry metadata differs from native observer");
            if(std::memcmp(&world_before,&world,sizeof(world)) || std::memcmp(&actors_before,&actors,sizeof(actors)))
                throw std::runtime_error("Read-only camera changed the authoritative world");
            std::cout<<"Read-only classified camera matches native result\n";
        }
        if(before.pc==0xfff13ad2u && argc==8) {
            ZrFont font;
            if(!ZrDecodeFont(font,original.rom.data(),original.rom.size())
               || !std::equal(font.begin(),font.end(),original.video.begin()+0x6000))
                throw std::runtime_error("Decoded stock font differs from native CHR atlas");
            const auto video_after=file(argv[7]);if(f.video!=video_after)throw std::runtime_error("Native raster VRAM mismatch");
            ZrViewGeometry view{};ZrEyePlanes planes{};
            std::copy_n(original.wram.begin()+0x5230,view.line.size(),view.line.begin());view.lines=before.gpr[6];
            const unsigned bank=original.wram[0x1196]&1;
            for(unsigned eye=0;eye<2;++eye)
                std::copy_n(original.video.begin()+eye*0x10000+bank*0x8000,0x6000,planes[eye].begin());
            if(!ZrRasterGeometry(view,planes,original.rom.data(),original.rom.size()))
                throw std::runtime_error("Classified raster bus refused native lines");
            for(unsigned eye=0;eye<2;++eye)if(!std::equal(planes[eye].begin(),planes[eye].end(),
                video_after.begin()+eye*0x10000+bank*0x8000))throw std::runtime_error("Eye raster pixels differ");
            std::cout<<"Independent left and right raster planes match stock pixels\n";
        }
        std::cout<<"{\"matched_native\":"<<(matched?"true":"false")<<",\"cycles\":"<<std::dec<<result.cycles
                 <<",\"read_addresses\":"<<f.reads.size()<<",\"write_addresses\":"<<f.writes.size()<<"}\n";
        std::cout<<"WRAM_READS";for(const auto& p:f.reads)std::cout<<' '<<std::hex<<(p.first&65535);std::cout<<'\n';
        // Refusing a bus write must not masquerade as a successful adapter.
        c=before;ZrRoutineBus reject{&original,Fixture::read,[](void*,uint32_t,unsigned,uint32_t){return false;}};
        const auto refused=ZrRoutineRun(c,reject,before.pc,expected.pc,1000000);
        if(refused.completed || !refused.failure_address)throw std::runtime_error("Rejected memory access reported success");
        return matched?0:1;
    } catch(const std::exception& error){std::cout<<error.what()<<'\n';return 1;}
}
