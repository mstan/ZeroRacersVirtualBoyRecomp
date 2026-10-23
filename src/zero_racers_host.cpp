// Development host bridge. Native VS menus/presentation and network launch
// will use this same canonical session; command-line entry is for automation.
#include "zero_racers_session.h"
#include "zero_racers_video.h"
#include "zero_racers_audio.h"
#include "game_host.h"
#include "simulation_points.h"
#include "memory.h"
#include "savestate.h"
#include "mod_runtime.h"
#include "stub_abort.h"
#include "host.h"
#include "v810_interpreter.h"
#include "input.h"
#include "netplay.h"
#include <cstdlib>
#include <cstring>
#include <cstdio>

namespace {
struct LaunchState {
    uint32_t players=0,course=1;
    std::array<unsigned,4> machines{0,1,2,3};
    uint32_t seed=1;
    uint32_t network=0,authority=0;
};
struct GameState {LaunchState launch;ZrSessionState session;std::array<ZrCameraState,4> cameras;ZrMenuState menu;
    std::array<ZrWarningState,4> warnings;uint32_t return_requested=0,return_tick=0;};
static_assert(sizeof(LaunchState)==36 && sizeof(ZrMenuState)==64 && sizeof(GameState)==4736,"Canonical launch/camera/menu/warning/return layout");
GameState game;
ZrSessionState& session=game.session;
LaunchState& launch=game.launch;
unsigned requested_players=0,requested_course=1;
std::array<unsigned,4> machines{0,1,2,3};
const char* error="";
const uint8_t* rom;
size_t rom_size;
bool points_ready;
LaunchState configuration(unsigned players,unsigned course,const std::array<unsigned,4>& selected,unsigned seed) {
    return {players,course,selected,seed,uint32_t(vb_netplay_enabled()!=0),unsigned(vb_netplay_authority_seat())};
}
uint32_t word(const uint8_t* p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void strict_native(CPUState* cpu,uint32_t entry,uint32_t return_pc) {
    vb_execution.mode=VB_EXEC_NATIVE;vb_dispatch_call(cpu,entry,return_pc);
}
ZrNativeCall selected_backend() {
    return vb_execution.mode==VB_EXEC_INTERPRETER?nullptr:
        vb_execution.mode==VB_EXEC_NATIVE?strict_native:vb_dispatch_call;
}
int option(int argc,char** argv,int* index,void*) {
    const char* argument=argv[*index];unsigned* target=nullptr;unsigned lo=0,hi=0,video_cache=1;
    if(!std::strcmp(argument,"--versus")){target=&requested_players;lo=2;hi=4;}
    else if(!std::strcmp(argument,"--versus-course")){target=&requested_course;lo=1;hi=16;}
#if defined(VBRECOMP_DEBUG_TOOLS)
    else if(!std::strcmp(argument,"--versus-video-cache")){target=&video_cache;hi=1;}
#endif
    else return 0;
    if(*index+1>=argc){error="Missing versus argument";return -1;}
    const char* text=argv[++*index];char* end=nullptr;const auto n=std::strtoul(text,&end,10);
    if(!*text || *text=='-' || *end || n<lo || n>hi){error="Invalid versus player count or course";return -1;}
    *target=unsigned(n);
    if(target==&video_cache)ZrVideoCacheEnable(video_cache!=0);
    if(target==&requested_players)vb_host_sandbox_sram();
    return 1;
}
int validate(const void* data,size_t size,void*) {
    if(size!=sizeof(game))return 0;
    GameState candidate;std::memcpy(&candidate,data,size);
    if(candidate.launch.network!=uint32_t(vb_netplay_enabled()!=0)
       || candidate.launch.authority!=unsigned(vb_netplay_authority_seat()))return 0;
    if(candidate.launch.network && candidate.launch.players!=unsigned(vb_netplay_players()))return 0;
    if(candidate.return_requested>1 || (!candidate.return_requested && candidate.return_tick)
       || (candidate.return_requested && (!candidate.launch.network || candidate.session.phase!=ZrRacePhase::Results)))return 0;
    if(!ZrMenuValidate(candidate.menu) || (candidate.menu.screen && candidate.session.phase!=ZrRacePhase::Empty
        && !(candidate.menu.screen==2 && candidate.session.phase==ZrRacePhase::Results)))return 0;
    if(candidate.launch.players && (candidate.launch.players<2 || candidate.launch.players>4))return 0;
    if(candidate.launch.course<1 || candidate.launch.course>16)return 0;
    for(unsigned machine:candidate.launch.machines)if(machine>=4)return 0;
    for(const auto& warning:candidate.warnings)if(!ZrWarningValidate(warning))return 0;
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
    if(launch.players) {
        if(!vb_host_begin_multiplayer_sram(vb_state_previous_sram()))error="Cannot protect native single-player progress";
    } else if(!vb_host_end_multiplayer_sram(false))error="Cannot preserve native single-player progress";
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
    cpu->write32(0x05001104,session.actors[0].native_1104);
    cpu->write32(0x05001148,session.actors[0].native_1148);
}
int point(CPUState* cpu,void*) {
    if(cpu->pc==0xfff21d12u && session.phase==ZrRacePhase::Empty && (!launch.players || vb_netplay_native_setup())) {
        auto& menu=game.menu;
        if(vb_netplay_native_setup() && !menu.screen) {
            menu.screen=2;menu.players=launch.players;menu.course=launch.course;menu.machines=launch.machines;
            menu.row=2+launch.authority;menu.ready_mask=0;menu.previous={};
        }
        if(menu.pending_launch) {
            if(!vb_host_begin_multiplayer_sram(nullptr,true)) {
                error="Cannot protect native single-player progress";menu.pending_launch=0;return 0;
            }
            launch=configuration(menu.players,menu.course,menu.machines,1);menu.screen=0;menu.pending_launch=0;menu.native_launch=1;
            cpu->gpr[23]=0;cpu->pc=0xfff221c0u;return 1;
        }
        if(vb_netplay_native_setup()){cpu->write32(0x05001024,0);return 0;}
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
    if(cpu->pc==0xfff0fe5au && session.phase==ZrRacePhase::Results && game.menu.pending_launch) {
        // Re-enter the real race initializer in its existing GP stack frame.
        // New course assets/music/devices follow the same native path as boot.
        game.menu.screen=0;cpu->pc=0xfff0fd1cu;return 1;
    }
    if(cpu->pc==0xfff10c70u && session.phase==ZrRacePhase::Ready) {
        // Native fly-in has completed. Versus owns its shared 150-field
        // countdown and the same original F3 beep/GO commands, exactly once.
        if(!ZrSessionStart(session))vb_stub_abort("versus countdown failed",cpu->pc,0);
        cpu->write8(0x050000f3,5);
        cpu->write16(0x0003d908,0x100);cpu->write16(0x0003d8e8,0x100);
        cpu->pc=0xfff10d30u;return 1;
    }
    if(cpu->pc==0xfff0ff64u) {
        if(game.menu.pending_launch && session.phase==ZrRacePhase::Results) {
            launch=configuration(game.menu.players,game.menu.course,game.menu.machines,launch.seed);
            session={};game.cameras={};game.menu.pending_launch=0;ZrVideoInvalidate();
        }
        if(launch.players) {
            cpu->write32(0x05001058,launch.course);cpu->write32(0x05001100,0);
            cpu->write32(0x050010fc,word(rom+0xddd94+launch.machines[0]*20));
        }
    } else if(cpu->pc==0xfff1047eu) {
        if(launch.players && !ZrSessionInitialize(session,launch.course,launch.machines,launch.players,launch.seed,rom,rom_size))
            vb_stub_abort("versus initialization failed",cpu->pc,launch.course);
        if(session.phase!=ZrRacePhase::Empty) {
            game.cameras={};
            game.warnings={};
            for(unsigned i=0;i<session.players;++i) {
                for(unsigned off=0;off<32;++off)game.warnings[i].power_world[off]=cpu->read8(0x0003db00+off);
                game.cameras[i].mode=1;
                if(!ZrCameraUpdatePose(game.cameras[i],session.actors,i,rom,rom_size,selected_backend()))
                    vb_stub_abort("versus camera initialization failed",cpu->pc,i);
            }
            sync_guest(cpu);
        }
    } else if(session.phase!=ZrRacePhase::Empty) {
        if(cpu->pc==0xfff0f4c4u) {
            if(session.phase==ZrRacePhase::Ready) {
                if(!ZrSessionStart(session))vb_stub_abort("versus countdown failed",cpu->pc,0);
                cpu->write8(0x050000f3,5);
            }
            sync_guest(cpu);cpu->pc=0xfff0f65au;return 1;
        }
        if(cpu->pc==0xfff0f77eu) {cpu->pc=0xfff0fa76u;return 1;}
        if(cpu->pc==0xfff0fa7au && session.phase!=ZrRacePhase::Ready) {
            cpu->pc=0xfff0fa7eu;return 1; // per-actor audio was sealed at end_field
        }
        if(cpu->pc==0xfff0fb8au && session.phase!=ZrRacePhase::Ready) {
            for(unsigned i=0;i<session.players;++i) {
                if(session.result[i].status!=ZrRacerStatus::Racing)continue;
                // Native HUD formatting (172B0) also advances the primary
                // damage-message latch. Respect that producer rather than
                // repeatedly announcing the same damage from a private zero.
                if(i==0)game.warnings[i].previous_damage=cpu->read32(0x05001168);
                ZrEnvironmentEvents commands;
                if(!ZrActorWarning(game.warnings[i],session.actors[i],cpu->read32(0x05001048),
                    rom,rom_size,commands,selected_backend()))vb_stub_abort("versus warning failed",cpu->pc,i);
                if(i==0) {
                    if(commands.written&1)cpu->write32(0x05001160,commands.native_1160);
                    cpu->write32(0x05001168,game.warnings[i].previous_damage);
                    cpu->write32(0x0500114c,game.warnings[i].model_bias);
                }
            }
            cpu->pc=0xfff0fb8eu;return 1;
        }
    }
    return 0;
}
int restart(CPUState*,void*) {
    game={};error="";
    rom=vb_rom_data();rom_size=vb_rom_size();
    if(!rom || rom_size!=1048576)return 0;
    if(!ZrVideoInitialize(rom,rom_size)){error="Cannot decode stock versus artwork";return 0;}
    launch=configuration(vb_netplay_enabled()?unsigned(vb_netplay_players()):requested_players,requested_course,machines,1);
    return !points_ready || vb_simulation_points_enable(1);
}
int initialize(CPUState* cpu,void*) {
    if(!restart(cpu,nullptr))return 0;
    if(!vb_state_register_simulation("zero-racers.versus.v3",&game,sizeof(game),validate,loaded,nullptr))return 0;
    for(uint32_t pc:{0xfff0ff64u,0xfff1047eu,0xfff0f4c4u,0xfff0f77eu,0xfff21d12u,0xfff221dcu,0xfff0fc5cu,
                     0xfff10c70u,0xfff0fa7au,0xfff0fe5au,0xfff0fb8au}) {
        const auto* instruction=rom+(pc&0xfffff);
        if(!vb_simulation_point_register(pc,uint16_t(instruction[0]|uint16_t(instruction[1])<<8),point,nullptr))return 0;
    }
    points_ready=true;
    return vb_simulation_points_enable(1);
}
void commit_environment(CPUState* cpu,const ZrEnvironmentEvents& e,bool primary) {
    if(primary && (e.written&1))cpu->write32(0x05001160,e.native_1160);
    if(e.written&2)cpu->write8(0x050000e2,e.native_e2);
    if(e.written&4)cpu->write8(0x050000f1,e.native_f1);
    if(e.written&8)cpu->write8(0x050000f2,e.native_f2);
}
void commit_human(CPUState* cpu,const ZrActorEvents& e,bool primary) {
    if(primary && (e.written&1))cpu->write32(0x05001160,e.native_1160);
    if(primary && (e.written&2))cpu->write32(0x0500116c,e.native_116c);
    if(e.written&4)cpu->write8(0x050000ee,e.native_ee);
    if(e.written&8)cpu->write8(0x050000f1,e.native_f1);
    if(e.written&16)cpu->write8(0x050000f2,e.native_f2);
    if(e.written&32)cpu->write8(0x050000f3,e.native_f3);
}
void field(CPUState* cpu,const VbGameInputs* bundle,void*) {
    if(game.return_requested)return;
    if(game.menu.screen==2) {
        std::array<uint16_t,4> pads{};for(unsigned i=0;i<4;++i)pads[i]=bundle->buttons[i];
        ZrMenuStep(game.menu,pads,launch.authority,launch.network!=0,!launch.network || session.phase==ZrRacePhase::Results);
        if(session.phase==ZrRacePhase::Results && game.menu.screen==1)game.menu.screen=0;
        return;
    }
    if(session.phase==ZrRacePhase::Empty || session.phase==ZrRacePhase::Ready)return;
    if(session.phase==ZrRacePhase::Results) {
        const uint16_t pressed=bundle->buttons[launch.authority]&~game.menu.previous[launch.authority];
        for(unsigned i=0;i<4;++i)game.menu.previous[i]=bundle->buttons[i];
        if(launch.network && (pressed&VB_PAD_SELECT)) {
            game.return_requested=1;game.return_tick=bundle->network_tick;return;
        }
        if(!launch.network && (pressed&VB_PAD_SELECT)) {
            if(!vb_host_end_multiplayer_sram(true)){error="Cannot preserve native single-player progress";return;}
            if(!vb_state_reset())vb_stub_abort("versus return to title failed",cpu->pc,0);
            return;
        }
        if(!game.menu.pending_launch && (pressed&(VB_PAD_START|VB_PAD_B))) {
            game.menu.players=launch.players;game.menu.course=launch.course;game.menu.machines=launch.machines;game.menu.row=0;
            if(pressed&VB_PAD_B){game.menu.screen=2;game.menu.ready_mask=0;}
            else {game.menu.pending_launch=1;game.menu.ready_mask=(1u<<launch.players)-1;}
        }
        return;
    }
    std::array<ZrSeatInput,4> inputs{};
    for(unsigned i=0;i<session.players;++i)inputs[i]={bundle->buttons[i],bundle->commands[i]};
    ZrSessionEvents events;
    const unsigned countdown=session.phase==ZrRacePhase::Countdown?session.countdown:0;
    const ZrNativeCall backend=selected_backend();
    if(!ZrSessionStep(session,inputs,rom,rom_size,events,backend))
        vb_stub_abort("versus shared tick failed",cpu->pc,session.tick);
    if(events.results)for(unsigned i=0;i<4;++i)game.menu.previous[i]=bundle->buttons[i];
    // Preserve the cartridge's finite shared command-channel behavior. Apply
    // emitted stores in the same stable human/contact/environment order as
    // the simulation; a later store to one native channel has priority.
    if(events.physics) {
        for(unsigned i=0;i<session.players;++i)commit_human(cpu,events.human[i],i==0);
        for(const auto& e:events.contact)commit_environment(cpu,e,false);
        for(unsigned i=0;i<session.players;++i) {
            commit_environment(cpu,events.surface[i],i==0);commit_environment(cpu,events.wall[i],i==0);
            commit_environment(cpu,events.repair[i],i==0);
        }
    }
    if(events.physics || events.results || (countdown && session.tick%3==0))
        for(unsigned i=0;i<session.players;++i) {
            if(!ZrCameraUpdatePose(game.cameras[i],session.actors,i,rom,rom_size,backend))
                vb_stub_abort("versus camera update failed",cpu->pc,i);
            ZrNearbyAudio audio;ZrEnvironmentEvents nearby;
            if(!ZrActorNearbyAudio(session.actors[i],session.actors,i,game.cameras[i],rom,rom_size,audio,nearby,backend))
                vb_stub_abort("versus nearby audio failed",cpu->pc,i);
            commit_environment(cpu,nearby,false);
            if(i==0) {
                cpu->write32(0x050007f4,audio.speed);cpu->write32(0x050007f8,audio.direction);
                cpu->write32(0x050007fc,audio.distance);
            }
        }
    if(countdown && session.phase==ZrRacePhase::Countdown
       && (session.countdown==100 || session.countdown==50))cpu->write8(0x050000f3,5);
    if(countdown && session.phase==ZrRacePhase::Racing)cpu->write8(0x050000f3,6);
}
const char* get_error(void*){return error;}
int can_pause(void*) {return !game.return_requested && session.phase!=ZrRacePhase::Countdown && session.phase!=ZrRacePhase::Racing;}
int lobby_return(uint32_t* tick,void*){*tick=game.return_tick;return game.return_requested!=0;}
int video_size(int seat,int* w,int* h,void*) {
    if(game.menu.screen && (seat==-1 || launch.network)){*w=384;*h=224;return 1;}
    return ZrVideoSize(session,seat,w,h);
}
int video_render(int seat,int eye,uint32_t* out,int w,int h,void*) {
    if(game.menu.screen && (seat==-1 || launch.network))return w==384 && h==224 && ZrVideoMenu(game.menu,eye,out);
    return ZrVideoRender(session,game.cameras,game.warnings,rom,rom_size,selected_backend(),seat,eye,out,w,h);
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
    append("],\"countdown\":%u,\"menu\":{\"screen\":%u,\"highlight\":%u,\"row\":%u,\"players\":%u,\"course\":%u,\"ready_mask\":%u}}",
           session.countdown,
           game.menu.screen,game.menu.highlight,game.menu.row,game.menu.players,game.menu.course,game.menu.ready_mask);
    if(used>=capacity)std::snprintf(out,capacity,"{\"ok\":false,\"error\":\"Game status capacity\"}");
    return 1;
}
}
VB_MOD_CONSTRUCTOR(register_versus_host) {
    const VbGameHost adapter{"zero-racers.versus",nullptr,option,get_error,initialize,field,can_pause,debug,video_size,video_render,4,restart,lobby_return};
    if(!vb_game_host_register(&adapter))vb_stub_abort("cannot register versus host",0,0);
}
