#include "zero_racers_session.h"
#include "input.h"
#include "vb_crc32.h"
#include "png_write.h"
#include "v810_interpreter.h"
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

extern "C" void vb_stub_abort(const char* reason,uint32_t pc,uint32_t address) {
    std::cerr<<reason<<" pc="<<std::hex<<pc<<" address="<<address<<'\n';std::abort();
}
extern "C" void vb_stub_abort_simple(const char* reason,uint32_t pc){vb_stub_abort(reason,pc,0);}
extern "C" void vb_fntrace_record(uint32_t,uint32_t){}
namespace {
#ifdef ZR_TEST_NATIVE
void strict_native(CPUState* cpu,uint32_t entry,uint32_t return_pc) {
    vb_execution.mode=VB_EXEC_NATIVE;vb_dispatch_call(cpu,entry,return_pc);
}
constexpr ZrNativeCall native=strict_native;
#else
constexpr ZrNativeCall native=nullptr;
#endif
std::ofstream trace;
uint32_t word(const uint8_t* p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void word(uint8_t* p,uint32_t value){for(unsigned i=0;i<4;++i)p[i]=uint8_t(value>>(8*i));}
void require(bool value,const char* label){if(!value)throw std::runtime_error(label);}
bool step(ZrSessionState& state,const std::array<ZrSeatInput,4>& inputs,
          const std::vector<uint8_t>& rom,ZrSessionEvents& events) {
    const bool ok=ZrSessionStep(state,inputs,rom.data(),rom.size(),events,native);
    if(ok && trace) {
        trace.write(reinterpret_cast<const char*>(&state),sizeof(state));
        // Commands are emitted once by the simulation. Serialize named fields
        // so no host struct padding can masquerade as an audio difference.
        auto scalar=[](uint32_t n){for(unsigned i=0;i<4;++i)trace.put(char(n>>(8*i)));};
        for(const auto& e:events.human) {
            scalar(e.written);
            scalar(e.native_1160);scalar(e.native_116c);scalar(e.native_ee);
            scalar(e.native_f1);scalar(e.native_f2);scalar(e.native_f3);
        }
        for(const auto& group:{events.surface,events.wall,events.repair})for(const auto& e:group) {
            scalar(e.written);
            scalar(e.native_1160);scalar(e.native_e2);scalar(e.native_f1);scalar(e.native_f2);
        }
        for(const auto& e:events.contact){scalar(e.written);scalar(e.native_f1);}
    }
    return ok;
}
}
int main(int argc,char** argv) {
    if(argc<2 || argc>3){std::cerr<<"ROM [private-output-directory]\n";return 2;}
    try {
        if(argc==3) {
            std::filesystem::create_directories(std::filesystem::u8path(argv[2]));
            trace.open(std::filesystem::u8path(argv[2])/"session-trace.bin",std::ios::binary);
            require(bool(trace),"Private trace destination");
        }
        std::ifstream input(std::filesystem::u8path(argv[1]),std::ios::binary);
        const std::vector<uint8_t> rom{std::istreambuf_iterator<char>(input),{}};
        require(rom.size()==1048576 && vb_crc32_compute(rom.data(),rom.size())==0x71553796,"Stock ROM identity");
        const std::array<unsigned,4> machines{0,1,2,3};
        std::array<ZrSeatInput,4> inputs{};ZrSessionState state;ZrSessionEvents events;
        unsigned configurations=0;
        for(unsigned players=2;players<=4;++players)for(unsigned course=1;course<=16;++course) {
            require(ZrSessionInitialize(state,course,machines,players,0x12345678,rom.data(),rom.size()),"Initialize native course/grid");
            require(ZrSessionStart(state),"Begin countdown");
            for(unsigned field=0;field<150;++field)require(step(state,{},rom,events),"Countdown step");
            require(state.phase==ZrRacePhase::Racing && state.race_fields==0,"Countdown has no race time");
            const auto initial=state.actors;
            for(unsigned field=0;field<24;++field) {
                for(unsigned i=0;i<players;++i)inputs[i].pad=uint16_t(VB_PAD_RT|(i&1?VB_PAD_LLEFT:VB_PAD_LRIGHT));
                if(!step(state,inputs,rom,events)) {
                    std::cerr<<"course="<<course<<" players="<<players<<" field="<<field<<'\n';
                    throw std::runtime_error("Shared native mechanics step");
                }
            }
            require(state.race_fields==24,"Three-field cadence preserves the display clock");++configurations;
            for(unsigned i=0;i<players;++i)
                require(word(state.actors[i].body.data()+0x34)>0
                    && std::memcmp(initial[i].body.data(),state.actors[i].body.data(),12),"Accelerator must move each human racer");
        }
        require(ZrSessionInitialize(state,11,machines,4,0x87654321,rom.data(),rom.size()),"Four-player fixture");
        state.phase=ZrRacePhase::Racing;inputs={};
        auto run=[&](ZrSessionState& s,unsigned first,unsigned last) {
            for(unsigned field=first;field<last;++field) {
                for(unsigned i=0;i<4;++i)inputs[i]={uint16_t(VB_PAD_RT|(((field/30+i)%3)==0?VB_PAD_LLEFT:0)),0};
                require(step(s,inputs,rom,events),"Live/replay input step");
            }
        };
        run(state,0,90);const auto checkpoint=state;run(state,90,180);const auto expected=state;
        state=checkpoint;run(state,90,180);
        require(!std::memcmp(&expected,&state,sizeof(state)),"Whole-session rollback replay must be byte identical");
        require(std::memcmp(state.actors[0].body.data(),state.actors[1].body.data(),12)!=0,"Independent racers");
        const auto before_menu=state.race_fields;inputs={};inputs[0].pad=VB_PAD_START;
        require(step(state,inputs,rom,events),"Live Start menu");
        require(state.menu_mask==1 && state.race_fields==before_menu+1,"Start must not pause multiplayer");
        // Native terminal progress boundary, independently for both humans.
        require(ZrSessionInitialize(state,11,machines,2,1,rom.data(),rom.size()),"Terminal fixture");
        state.phase=ZrRacePhase::Racing;
        const auto waypoint=word(rom.data()+0xcb058+11*16)&0xfffff;
        for(unsigned i=0;i<2;++i) {
            auto* body=state.actors[i].body.data();std::memcpy(body,rom.data()+waypoint,12);
            word(body+0x48,4);word(body+0x50,0);
        }
        require(step(state,{},rom,events),"Terminal native crossing");
        require(state.phase==ZrRacePhase::Results && state.result[0].status==ZrRacerStatus::Finished
            && state.result[1].status==ZrRacerStatus::Finished
            && state.result[0].finish_field==state.result[1].finish_field,"Same-field finish tie");
        require(ZrSessionInitialize(state,1,machines,3,1,rom.data(),rom.size()),"Survivor fixture");
        state.phase=ZrRacePhase::Racing;inputs={};inputs[0].command=inputs[1].command=ZrInputForfeit;
        require(step(state,inputs,rom,events),"Forfeit sealed commands");
        require(state.phase==ZrRacePhase::Results && state.result[2].finish_field==0,"Last survivor has no invented finish time");
        // Derived views may differ; producing/repeating them cannot mutate the
        // authoritative state used by the rollback comparison above.
        state=expected;const auto canonical=state;std::array<ZrEyePlanes,4> pictures{};
        for(unsigned i=0;i<4;++i) {
            ZrViewGeometry view{};view.camera.mode=1;
            require(ZrCameraBuild(view,state.world,state.actors,i,rom.data(),rom.size(),native),"Independent camera build");
            require(ZrRasterGeometry(view,pictures[i],rom.data(),rom.size(),native),"Both eye raster planes");
            ZrEyePlanes repeated{};
            require(ZrRasterGeometry(view,repeated,rom.data(),rom.size(),native) && pictures[i]==repeated,"Repeated presentation");
            if(argc==3) {
                std::filesystem::create_directories(std::filesystem::u8path(argv[2]));
                std::vector<uint32_t> argb(384*224);
                for(unsigned eye=0;eye<2;++eye) {
                    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<384;++x) {
                        const auto level=(pictures[i][eye][x*64+y/4]>>((y%4)*2))&3;
                        argb[y*384+x]=0xff000000u|(level*85u<<16);
                    }
                    auto path=std::filesystem::u8path(argv[2])/('p'+std::to_string(i+1)+"-eye"+std::to_string(eye)+".png");
                    require(vb_write_png_32bpp(path.u8string().c_str(),384,224,argb.data())==0,"Write private native view");
                }
            }
        }
        require(!std::memcmp(&canonical,&state,sizeof(state)),"Rendering changed canonical race state");
        require(pictures[0][0]!=pictures[1][0] && pictures[1][0]!=pictures[2][0]
            && pictures[2][0]!=pictures[3][0],"Each racer must have a distinct camera");
        std::cout<<"{\"passed\":true,\"native_grid_configurations\":"<<configurations
                 <<",\"rollback_replay_fields\":90,\"independent_views\":4,\"eyes_per_view\":2}\n";
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
