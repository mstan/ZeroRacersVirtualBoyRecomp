#include "zero_racers_session.h"
#include "zero_racers_menu.h"
#include "input.h"
#include "vb_crc32.h"
#include "png_write.h"
#include "v810_interpreter.h"
#include <cassert>
#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
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
        {
            ZrSessionState results;results.players=4;results.race_fields=200;
            results.result={{{ZrRacerStatus::Finished,150},{ZrRacerStatus::Crashed,0},
                {ZrRacerStatus::Finished,100},{ZrRacerStatus::Finished,100}}};
            const auto order=ZrSessionStandings(results);
            require(order.seats==std::array<unsigned,4>{2,3,0,1}
                && order.places==std::array<unsigned,4>{3,0,1,1},"Finish order, same-field competition tie, unclassified crash");
            require(ZrSessionClock(results,0)==150 && ZrSessionClock(results,1)==200,
                "A finisher's clock freezes while the shared race clock advances");
            results.result[0]={ZrRacerStatus::Forfeit,0};results.result[2]={ZrRacerStatus::Forfeit,0};
            results.result[3]={ZrRacerStatus::Survived,0};
            const auto survivor=ZrSessionStandings(results);
            require(survivor.seats[0]==3 && survivor.places==std::array<unsigned,4>{0,0,0,1}
                && results.result[3].finish_field==0,"Last survivor ranks first without a fabricated time");
        }
        for(unsigned authority=0;authority<4;++authority) {
            ZrMenuState menu;menu.screen=2;menu.players=4;menu.row=0;
            std::array<uint16_t,4> pads{};pads[authority]=VB_PAD_LRIGHT;
            ZrMenuStep(menu,pads,authority,true,false);require(menu.players==4,"Online roster is fixed");
            ZrMenuStep(menu,{},authority,true,false);menu.row=1;
            pads={};pads[(authority+1)%4]=VB_PAD_LRIGHT;
            ZrMenuStep(menu,pads,authority,true,false);require(menu.course==1,"Guest cannot change the course");
            require(menu.machines[(authority+1)%4]==machines[(authority+1)%4]+1
                || (machines[(authority+1)%4]==3 && menu.machines[(authority+1)%4]==0),"Guest selects own machine");
            ZrMenuStep(menu,{},authority,true,false);pads={};pads[authority]=VB_PAD_LRIGHT;
            ZrMenuStep(menu,pads,authority,true,false);require(menu.course==2,"Moved host retains course authority");
            ZrMenuStep(menu,{},authority,true,false);pads.fill(VB_PAD_A);menu.row=2+authority;
            ZrMenuStep(menu,pads,authority,true,false);require(menu.ready_mask==15,"Every human readies its own seat");
            ZrMenuStep(menu,{},authority,true,false);pads={};pads[authority]=VB_PAD_START;
            ZrMenuStep(menu,pads,authority,true,false);require(menu.pending_launch==1,"Moved host starts a ready field");
        }
        std::array<ZrSeatInput,4> inputs{};ZrSessionState state;ZrSessionEvents events;
        unsigned configurations=0;
        unsigned progress_courses=0;
        for(unsigned course=1;course<=16;++course) {
            require(ZrSessionInitialize(state,course,machines,2,1,rom.data(),rom.size()),"Progress grid fixture");
            const auto* header=rom.data()+0xcb054+course*16;
            const unsigned count=word(header),points=word(header+4)&0xfffff;
            auto& actor=state.actors[0];bool crossed=false;
            // Move through every actual waypoint, preserving the native lap
            // and wrap guards. No completion flag/counter is injected.
            std::copy_n(rom.data()+points,12,actor.body.data());
            require(ZrActorProgress(actor,state.world,rom.data(),rom.size(),crossed,native)
                && !crossed && word(actor.body.data()+0x48)==0 && word(actor.body.data()+0x50)==0,
                "Initial start crossing consumes the grid wrap guard");
            for(unsigned circuit=1;circuit<=5;++circuit) {
                for(unsigned waypoint=1;waypoint<=count;++waypoint) {
                    std::copy_n(rom.data()+points+(waypoint%count)*16,12,actor.body.data());
                    require(ZrActorProgress(actor,state.world,rom.data(),rom.size(),crossed,native),"Native waypoint progression");
                    require(word(actor.body.data()+0x3c)==waypoint%count,"Progress follows course topology");
                    require(crossed==(waypoint==count),"Exactly one completed-circuit event");
                }
                require(word(actor.body.data()+0x48)==circuit,"Native lap counter counts completed circuits");
            }
            ++progress_courses;
        }
        for(unsigned players=2;players<=4;++players)for(unsigned course=1;course<=16;++course) {
            require(ZrSessionInitialize(state,course,machines,players,0x12345678,rom.data(),rom.size()),"Initialize native course/grid");
            require(ZrSessionStart(state),"Begin countdown");
            for(unsigned field=0;field<ZrCountdownFields;++field)require(step(state,{},rom,events),"Countdown step");
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
        inputs[0].pad=VB_PAD_RT|VB_PAD_A;
        require(step(state,inputs,rom,events),"Menu held inputs");
        inputs[0].pad|=VB_PAD_START;
        require(step(state,inputs,rom,events),"Close menu while holding controls");
        require(!state.menu_mask && state.blocked_buttons[0]==(VB_PAD_RT|VB_PAD_A),"Menu controls suppressed until release");
        inputs[0].pad=0;require(step(state,inputs,rom,events),"Release blocked controls");
        require(!state.blocked_buttons[0],"Released controls become available");
        inputs[0].pad=VB_PAD_START;require(step(state,inputs,rom,events),"Open retire menu");
        inputs[0].pad=VB_PAD_SELECT;require(step(state,inputs,rom,events),"Select retires only this racer");
        require(state.result[0].status==ZrRacerStatus::Forfeit && state.result[1].status==ZrRacerStatus::Racing
            && state.phase==ZrRacePhase::Racing,"Live retire leaves other racers running");
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
        require(state.phase==ZrRacePhase::Results && state.result[2].status==ZrRacerStatus::Survived
            && state.result[2].finish_field==0 && word(state.actors[2].body.data()+0x34)==0,"Last survivor has no invented finish time or running engine");
        // Build all human repair beams at authored repair rectangles. The
        // view list keeps sparks, then beams in seat order, then course art.
        for(unsigned course=1;course<=16;++course) {
            require(ZrSessionInitialize(state,course,machines,4,1,rom.data(),rom.size()),"Repair view fixture");
            const auto* bounds=rom.data()+0xe35c4+course*16;
            const int32_t x=int32_t((int64_t(int32_t(word(bounds)))+int32_t(word(bounds+4)))/2);
            const int32_t z=int32_t((int64_t(int32_t(word(bounds+8)))+int32_t(word(bounds+12)))/2);
            for(unsigned i=0;i<4;++i) {
                auto& a=state.actors[i];word(a.body.data(),uint32_t(x));word(a.body.data()+8,uint32_t(z));
                word(a.body.data()+0x38,500);a.native_1148=0;
                ZrEnvironmentEvents repair;
                require(ZrActorEnvironment(ZrEnvironment::Repair,a,state.world,i,rom.data(),rom.size(),repair,native),"Repair beam preparation");
            }
            ZrWorldSyncRacerModels(state.world,state.actors);const auto unchanged=state;
            ZrModelDisplayList list;
            require(ZrComposeRepairModels(list,state.world,state.actors,4,rom.data(),rom.size()),"Four-player draw list");
            const unsigned count=word(rom.data()+0xcb054+course*16+8);
            require(list.count==count+12 && std::equal(state.world.models.begin(),state.world.models.begin()+12*32,list.models.begin())
                && std::equal(state.world.models.begin()+16*32,state.world.models.begin()+count*32,list.models.begin()+28*32),"Original draw ordering");
            for(unsigned i=0;i<4;++i) {
                require(std::equal(state.actors[i].repair_models.begin(),state.actors[i].repair_models.end(),list.models.begin()+(12+i*4)*32),"Every seat's repair beams");
                ZrCameraState camera;camera.mode=1;ZrViewGeometry view;
                for(unsigned n=0;n<20;++n)require(ZrCameraUpdatePose(camera,state.actors,i,rom.data(),rom.size(),native),"Repair camera");
                require(ZrCameraBuildFromPose(view,camera,state.world,state.actors,i,rom.data(),rom.size(),native,0,4),"Simultaneous repair geometry capacity");
                ZrEyePlanes planes{};require(ZrRasterGeometry(view,planes,rom.data(),rom.size(),native),"Repair both-eye raster");
                if(trace)trace.write(reinterpret_cast<const char*>(&planes),sizeof(planes));
            }
            require(!std::memcmp(&state,&unchanged,sizeof(state)),"Repair rendering changed authoritative state");
        }
        // Derived views may differ; producing/repeating them cannot mutate the
        // authoritative state used by the rollback comparison above.
        state=expected;const auto canonical=state;std::array<ZrEyePlanes,4> pictures{};
        for(unsigned i=0;i<4;++i) {
            ZrCameraState camera{};camera.mode=1; // stock gameplay follow camera
            for(unsigned field=0;field<20;++field)
                require(ZrCameraUpdatePose(camera,state.actors,i,rom.data(),rom.size(),native),"Logical camera cadence");
            const auto frozen=camera;ZrViewGeometry view{};
            require(ZrCameraBuildFromPose(view,camera,state.world,state.actors,i,rom.data(),rom.size(),native),"Independent frozen camera build");
            require(ZrRasterGeometry(view,pictures[i],rom.data(),rom.size(),native),"Both eye raster planes");
            ZrEyePlanes repeated{};
            ZrViewGeometry repeated_view{};
            require(ZrCameraBuildFromPose(repeated_view,camera,state.world,state.actors,i,rom.data(),rom.size(),native)
                && ZrRasterGeometry(repeated_view,repeated,rom.data(),rom.size(),native) && pictures[i]==repeated
                && !std::memcmp(&camera,&frozen,sizeof(camera)),"Repeated presentation must not advance a camera");
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
        std::cout<<"{\"passed\":true,\"native_grid_configurations\":"<<configurations<<",\"progress_courses\":"<<progress_courses
                 <<",\"rollback_replay_fields\":90,\"independent_views\":4,\"eyes_per_view\":2}\n";
        return 0;
    } catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
}
