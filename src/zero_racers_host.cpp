// Development host bridge. Native VS menus/presentation and network launch
// will use this same canonical session; command-line entry is for automation.
#include "zero_racers_session.h"
#include "zero_racers_video.h"
#include "game_host.h"
#include "simulation_points.h"
#include "memory.h"
#include "savestate.h"
#include "mod_runtime.h"
#include "stub_abort.h"
#include "host.h"
#include "v810_interpreter.h"
#include "input.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace {
struct LaunchState {
    uint32_t players=0,course=1;
    std::array<unsigned,4> machines{0,1,2,3};
    uint32_t seed=1;
};
struct GameState {LaunchState launch;ZrSessionState session;std::array<ZrCameraState,4> cameras;ZrMenuState menu;};
static_assert(sizeof(LaunchState)==28 && sizeof(ZrMenuState)==64 && sizeof(GameState)==4544,"Canonical launch/camera/menu layout");
GameState game;
ZrSessionState& session=game.session;
LaunchState& launch=game.launch;
unsigned requested_players=0,requested_course=1;
std::array<unsigned,4> machines{0,1,2,3};
const char* error="";
const uint8_t* rom;
size_t rom_size;
uint32_t word(const uint8_t* p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void strict_native(CPUState* cpu,uint32_t entry,uint32_t return_pc) {
    vb_execution.mode=VB_EXEC_NATIVE;vb_dispatch_call(cpu,entry,return_pc);
}
ZrNativeCall selected_backend() {
    return vb_execution.mode==VB_EXEC_INTERPRETER?nullptr:
        vb_execution.mode==VB_EXEC_NATIVE?strict_native:vb_dispatch_call;
}
int option(int argc,char** argv,int* index,void*) {
    const char* argument=argv[*index];unsigned* target=nullptr;unsigned lo=0,hi=0;
    if(!std::strcmp(argument,"--versus")){target=&requested_players;lo=2;hi=4;}
    else if(!std::strcmp(argument,"--versus-course")){target=&requested_course;lo=1;hi=16;}
    else return 0;
    if(*index+1>=argc){error="Missing versus argument";return -1;}
    const char* text=argv[++*index];char* end=nullptr;const auto n=std::strtoul(text,&end,10);
    if(!*text || *text=='-' || *end || n<lo || n>hi){error="Invalid versus player count or course";return -1;}
    *target=unsigned(n);
    if(target==&requested_players)vb_host_sandbox_sram();
    return 1;
}
int validate(const void* data,size_t size,void*) {
    if(size!=sizeof(game))return 0;
    GameState candidate;std::memcpy(&candidate,data,size);
    if(!ZrMenuValidate(candidate.menu) || (candidate.menu.screen && candidate.session.phase!=ZrRacePhase::Empty))return 0;
    if(candidate.launch.players && (candidate.launch.players<2 || candidate.launch.players>4))return 0;
    if(candidate.launch.course<1 || candidate.launch.course>16)return 0;
    for(unsigned machine:candidate.launch.machines)if(machine>=4)return 0;
    for(unsigned i=0;i<candidate.cameras.size();++i)
        if(candidate.cameras[i].mode!=(candidate.session.phase!=ZrRacePhase::Empty && i<candidate.session.players?1u:0u))return 0;
    if(candidate.session.phase!=ZrRacePhase::Empty
       && (candidate.session.players!=candidate.launch.players || candidate.session.world.course!=candidate.launch.course))return 0;
    return ZrSessionValidate(candidate.session,rom,rom_size);
}
void loaded(void*) {
    ZrVideoInvalidate();
    // The exact simulation profile stays fixed; restoring only changes whether
    // the loaded session is active. All derived camera caches are separate.
    vb_simulation_points_enable(1);
    if(launch.players)vb_host_sandbox_sram();
}
void sync_guest(CPUState* cpu) {
    for(unsigned i=0;i<session.actors.size();++i)
        for(unsigned off=0;off<0xa0;off+=4)
            cpu->write32(0x0500db70+i*0xa0+off,word(session.actors[i].body.data()+off));
    for(unsigned off=0;off<session.world.models.size();off+=4)
        cpu->write32(0x0500d270+off,word(session.world.models.data()+off));
    cpu->write32(0x05001100,0);cpu->write32(0x050010fc,session.world.native_10fc);
    cpu->write32(0x05001028,session.world.random);
    cpu->write32(0x050010d4,session.race_fields*2u);
    cpu->write32(0x050010f4,0);cpu->write32(0x050010f8,0);
    cpu->write32(0x05001140,session.actors[0].native_1140);
    cpu->write32(0x05001144,session.actors[0].native_1144);
}
int point(CPUState* cpu,void*) {
    if(cpu->pc==0xfff21d12u && session.phase==ZrRacePhase::Empty && !launch.players) {
        auto& menu=game.menu;
        if(menu.pending_launch) {
            launch={menu.players,menu.course,menu.machines,1};menu.screen=0;menu.pending_launch=0;menu.native_launch=1;
            vb_host_sandbox_sram();cpu->gpr[23]=0;cpu->pc=0xfff221c0u;return 1;
        }
        const uint32_t packed=cpu->read32(0x05001024);const uint16_t pressed=packed>>16;
        if(menu.screen==2)cpu->write32(0x05001024,0);
        else {
            menu.screen=1;
            if(menu.highlight) {
                if(pressed&(VB_PAD_LUP|VB_PAD_RUP)){menu.highlight=0;cpu->write32(0x05001024,0);}
                else if(pressed&(VB_PAD_A|VB_PAD_START)) {
                    menu.screen=2;menu.ready_mask=0;menu.previous={};menu.previous[0]=packed&65535;
                    cpu->write32(0x05001024,0);
                } else cpu->write32(0x05001024,0);
            } else if(cpu->gpr[23]==3 && (pressed&(VB_PAD_LDOWN|VB_PAD_RDOWN))) {
                menu.highlight=1;cpu->write32(0x05001024,0);
            }
        }
    }
    if(cpu->pc==0xfff221dcu && game.menu.screen!=2)game.menu.screen=0;
    if(cpu->pc==0xfff0fc5cu && launch.players && session.phase==ZrRacePhase::Empty && game.menu.native_launch) {
        cpu->pc=0xfff0fd1cu;return 1;
    }
    if(cpu->pc==0xfff0ff64u) {
        if(launch.players) {
            cpu->write32(0x05001058,launch.course);cpu->write32(0x05001100,0);
            cpu->write32(0x050010fc,word(rom+0xddd94+launch.machines[0]*20));
        }
    } else if(cpu->pc==0xfff1047eu) {
        if(launch.players && !ZrSessionInitialize(session,launch.course,launch.machines,launch.players,launch.seed,rom,rom_size))
            vb_stub_abort("versus initialization failed",cpu->pc,launch.course);
        if(session.phase!=ZrRacePhase::Empty) {
            game.cameras={};
            for(unsigned i=0;i<session.players;++i) {
                game.cameras[i].mode=1;
                if(!ZrCameraUpdatePose(game.cameras[i],session.actors,i,rom,rom_size,selected_backend()))
                    vb_stub_abort("versus camera initialization failed",cpu->pc,i);
            }
            sync_guest(cpu);
        }
    } else if(session.phase!=ZrRacePhase::Empty) {
        if(cpu->pc==0xfff0f4c4u) {
            if(session.phase==ZrRacePhase::Ready && !ZrSessionStart(session))
                vb_stub_abort("versus countdown failed",cpu->pc,0);
            sync_guest(cpu);cpu->pc=0xfff0f65au;return 1;
        }
        if(cpu->pc==0xfff0f77eu) {cpu->pc=0xfff0fa76u;return 1;}
    }
    return 0;
}
int initialize(CPUState*,void*) {
    rom=vb_rom_data();rom_size=vb_rom_size();
    if(!rom || rom_size!=1048576)return 0;
    if(!ZrVideoInitialize(rom,rom_size)){error="Cannot decode stock versus artwork";return 0;}
    launch={requested_players,requested_course,machines,1};
    if(!vb_state_register_simulation("zero-racers.versus.v1",&game,sizeof(game),validate,loaded,nullptr))return 0;
    for(uint32_t pc:{0xfff0ff64u,0xfff1047eu,0xfff0f4c4u,0xfff0f77eu,0xfff21d12u,0xfff221dcu,0xfff0fc5cu}) {
        const auto* instruction=rom+(pc&0xfffff);
        if(!vb_simulation_point_register(pc,uint16_t(instruction[0]|uint16_t(instruction[1])<<8),point,nullptr))return 0;
    }
    return vb_simulation_points_enable(1);
}
void field(CPUState* cpu,const VbGameInputs* bundle,void*) {
    if(game.menu.screen==2) {
        std::array<uint16_t,4> pads{};for(unsigned i=0;i<4;++i)pads[i]=bundle->buttons[i];
        ZrMenuStep(game.menu,pads);return;
    }
    if(session.phase==ZrRacePhase::Empty || session.phase==ZrRacePhase::Ready)return;
    std::array<ZrSeatInput,4> inputs{};
    for(unsigned i=0;i<session.players;++i)inputs[i]={bundle->buttons[i],bundle->commands[i]};
    ZrSessionEvents events;
    const ZrNativeCall backend=selected_backend();
    if(!ZrSessionStep(session,inputs,rom,rom_size,events,backend))
        vb_stub_abort("versus shared tick failed",cpu->pc,session.tick);
    if(events.physics || (session.phase==ZrRacePhase::Countdown && session.tick%3==0))
        for(unsigned i=0;i<session.players;++i)
            if(!ZrCameraUpdatePose(game.cameras[i],session.actors,i,rom,rom_size,backend))
                vb_stub_abort("versus camera update failed",cpu->pc,i);
    // Command consumption/audio mixing is connected in the next host stage.
}
const char* get_error(void*){return error;}
int can_pause(void*) {return session.phase!=ZrRacePhase::Countdown && session.phase!=ZrRacePhase::Racing;}
int video_size(int seat,int* w,int* h,void*) {
    if(game.menu.screen && seat==-1){*w=384;*h=224;return 1;}
    return ZrVideoSize(session,seat,w,h);
}
int video_render(int seat,int eye,uint32_t* out,int w,int h,void*) {
    if(game.menu.screen && seat==-1)return w==384 && h==224 && ZrVideoMenu(game.menu,eye,out);
    return ZrVideoRender(session,game.cameras,rom,rom_size,selected_backend(),seat,eye,out,w,h);
}
int debug(const char* command,long long id,char* out,size_t capacity,void*) {
    if(std::strcmp(command,"game_status"))return 0;
    size_t used=0;
    auto append=[&](const char* format,auto... values) {
        if(used>=capacity)return;
        int n=std::snprintf(out+used,capacity-used,format,values...);
        if(n<0 || size_t(n)>=capacity-used){used=capacity;return;}
        used+=size_t(n);
    };
    append("{\"ok\":true,\"id\":%lld,\"game\":\"zero-racers\",\"phase\":%u,\"players\":%u,\"course\":%u,\"tick\":%u,\"race_fields\":%u,\"can_pause\":%s,\"actors\":[",
           id,unsigned(session.phase),session.players,launch.course,session.tick,session.race_fields,can_pause(nullptr)?"true":"false");
    for(unsigned i=0;i<session.players;++i) {
        const auto* body=session.actors[i].body.data();
        append("%s{\"slot\":%u,\"x\":%d,\"y\":%d,\"z\":%d,\"speed\":%u,\"damage\":%u,\"sector\":%u,\"lap_counter\":%u,\"status\":%u,\"finish_field\":%u}",
               i?",":"",i,int32_t(word(body)),int32_t(word(body+4)),int32_t(word(body+8)),word(body+0x34),word(body+0x38),
               word(body+0x3c),word(body+0x48),unsigned(session.result[i].status),session.result[i].finish_field);
    }
    append("],\"menu\":{\"screen\":%u,\"highlight\":%u,\"row\":%u,\"players\":%u,\"course\":%u,\"ready_mask\":%u}}",
           game.menu.screen,game.menu.highlight,game.menu.row,game.menu.players,game.menu.course,game.menu.ready_mask);
    if(used>=capacity)std::snprintf(out,capacity,"{\"ok\":false,\"error\":\"Game status capacity\"}");
    return 1;
}
}
VB_MOD_CONSTRUCTOR(register_versus_host) {
    const VbGameHost adapter{"zero-racers.versus",nullptr,option,get_error,initialize,field,can_pause,debug,video_size,video_render};
    if(!vb_game_host_register(&adapter))vb_stub_abort("cannot register versus host",0,0);
}
