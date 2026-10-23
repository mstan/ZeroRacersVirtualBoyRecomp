#include "zero_racers_session.h"
#include "input.h"
#include <algorithm>
#include <limits>

namespace {
uint32_t word(const uint8_t* p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void word(uint8_t* p,uint32_t value){for(unsigned i=0;i<4;++i)p[i]=uint8_t(value>>(8*i));}
bool cartridge(const uint8_t* rom,size_t size){return rom && size==1048576;}
bool machine(uint32_t id){return id>=257 && id<=263 && (id&1);}
bool racing(const ZrSessionState& s,unsigned i){return s.result[i].status==ZrRacerStatus::Racing;}
bool settle(ZrSessionState& state) {
    unsigned remaining=0,survivors=0;
    for(unsigned i=0;i<state.players;++i) {
        remaining+=racing(state,i);
        survivors+=state.result[i].status!=ZrRacerStatus::Crashed && state.result[i].status!=ZrRacerStatus::Forfeit;
    }
    if(!remaining || survivors<=1) {
        if(survivors==1)for(unsigned i=0;i<state.players;++i)
            if(racing(state,i))state.result[i].status=ZrRacerStatus::Survived;
        state.phase=ZrRacePhase::Results;state.menu_mask=0;return true;
    }
    return false;
}
}
void ZrWorldSyncRacerModels(ZrWorldState& world,const std::array<ZrActorState,6>& actors) {
    // Exact scalar copy performed by stock FFF20C36. The actor representation
    // remains authoritative; model instances are the native shared draw list.
    for(unsigned i=0;i<actors.size();++i) {
        auto* out=world.models.data()+i*32;const auto* body=actors[i].body.data();
        if(!word(body+0x30)){std::fill_n(out,32,0);continue;}
        std::copy_n(body,12,out);std::copy_n(body+0x18,12,out+12);
        std::copy_n(body+0x30,4,out+24);std::copy_n(body+0x3c,4,out+28);
    }
}
void ZrWorldAdvanceEffects(ZrWorldState& world) {
    // FFF1F958: collision sparks advance once per simulation update.
    for(unsigned i=6;i<12;++i) {
        auto* p=world.models.data()+i*32+24;const uint32_t id=word(p);
        word(p,id==0x75 || id==0x79 || id==0x7d?0:id? id+1:0);
    }
    // FFF1FB9C: the original twenty interactive surface instances.
    for(unsigned i=19;i<39;++i) {
        auto* p=world.models.data()+i*32+24;const uint32_t id=word(p);
        if(id)word(p,0x140+((id-0x140)&~7u)+((id-0x140+1)&7u));
    }
}
bool ZrSessionInitialize(ZrSessionState& state,unsigned course,
                         const std::array<unsigned,ZrMaxPlayers>& machines,unsigned players,
                         uint32_t seed,const uint8_t* rom,size_t size) {
    if(!cartridge(rom,size) || course<1 || course>16 || players<2 || players>ZrMaxPlayers)return false;
    for(unsigned i=0;i<players;++i)if(machines[i]>=4)return false;
    const auto* header=rom+0xcb054+course*16;
    const uint32_t sectors=word(header),model_count=word(header+8),model_source=word(header+12)&0xfffffu;
    if(!sectors || model_count<19 || model_count>72 || model_source+(model_count-18)*32>size)return false;
    ZrSessionState next{};next.phase=ZrRacePhase::Ready;next.players=players;
    next.world.course=course;next.world.random=seed;
    // FFF102DC copies the course's authored list starting at native model18.
    std::copy_n(rom+model_source,(model_count-18)*32,next.world.models.begin()+18*32);
    const uint32_t marker=word(rom+0xde000+(course-1)*4)?0x36:0x3b;
    word(next.world.models.data()+18*32+24,marker);
    for(unsigned i=0;i<players;++i) {
        auto& actor=next.actors[i];auto* body=actor.body.data();
        const auto* grid=rom+0xdd594+(course-1)*128+i*32;
        const auto* profile=rom+0xddd94+machines[i]*20;
        std::copy_n(grid,12,body);std::copy_n(grid+12,12,body+0x18);
        for(auto field:{std::pair<unsigned,unsigned>{0x30,0},{0x54,4},{0x58,8},{0x60,12},{0x64,16}})
            std::copy_n(profile+field.second,4,body+field.first);
        // Native setup clears +58 for the selected human and allocates +64
        // only to CPU entrants. Every versus entrant receives the human form.
        word(body+0x58,0);word(body+0x64,0);
        word(body+0x3c,sectors-1);word(body+0x4c,1);word(body+0x50,1);
        actor.native_1104=UINT32_MAX;
        next.result[i].status=ZrRacerStatus::Racing;
    }
    next.world.native_10fc=word(next.actors[0].body.data()+0x30);
    ZrWorldSyncRacerModels(next.world,next.actors);
    if(!ZrSessionValidate(next,rom,size))return false;
    state=next;return true;
}
bool ZrSessionStart(ZrSessionState& state) {
    if(state.phase!=ZrRacePhase::Ready)return false;
    state.phase=ZrRacePhase::Countdown;state.countdown=150;state.menu_mask=0;return true;
}
bool ZrSessionValidate(const ZrSessionState& s,const uint8_t* rom,size_t size) {
    if(!cartridge(rom,size) || s.version!=1 || s.physics_phase>=3 || s.players>ZrMaxPlayers
       || s.phase>ZrRacePhase::Results)return false;
    if(s.phase==ZrRacePhase::Empty)return s.players==0;
    if(s.players<2 || s.world.course<1 || s.world.course>16 || !machine(s.world.native_10fc)
       || s.menu_mask>>s.players || s.countdown>150 || s.race_fields>s.tick)return false;
    const uint32_t sectors=word(rom+0xcb054+s.world.course*16);
    for(unsigned i=0;i<s.result.size();++i) {
        if(s.blocked_buttons[i]>65535 || (i>=s.players && s.blocked_buttons[i]))return false;
        const auto& result=s.result[i];
        if(i>=s.players) {if(result.status!=ZrRacerStatus::Absent || result.finish_field)return false;continue;}
        const auto* body=s.actors[i].body.data();
        if(result.status<ZrRacerStatus::Racing || result.status>ZrRacerStatus::Survived
           || (result.status==ZrRacerStatus::Survived && s.phase!=ZrRacePhase::Results)
           || (result.status==ZrRacerStatus::Racing && s.phase==ZrRacePhase::Results)
           || !machine(word(body+0x30)) || word(body+0x60)>=4 || word(body+0x3c)>=sectors
           || word(body+0x38)>800 || word(body+0x48)>5 || result.finish_field>s.race_fields
           || (result.status==ZrRacerStatus::Finished)!=bool(result.finish_field))return false;
    }
    for(unsigned i=s.players;i<s.actors.size();++i)
        if(std::any_of(s.actors[i].body.begin(),s.actors[i].body.end(),[](uint8_t b){return b!=0;}))return false;
    return true;
}
bool ZrSessionStep(ZrSessionState& state,const std::array<ZrSeatInput,ZrMaxPlayers>& inputs,
                    const uint8_t* rom,size_t size,ZrSessionEvents& events,ZrNativeCall native) {
    if(!ZrSessionValidate(state,rom,size) || state.phase==ZrRacePhase::Empty || state.tick==UINT32_MAX)return false;
    for(unsigned i=0;i<state.players;++i)if(inputs[i].command&~ZrInputForfeit)return false;
    auto next=state;ZrSessionEvents output{};++next.tick;
    if(next.phase==ZrRacePhase::Ready || next.phase==ZrRacePhase::Results) {state=next;events=output;return true;}
    // Start is a live menu gesture. It does not stop the world clock.
    for(unsigned i=0;i<next.players;++i) {
        const uint16_t previous=next.actors[i].menu_buttons;
        const uint16_t held=inputs[i].pad;next.actors[i].menu_buttons=held;
        const bool was_open=(next.menu_mask&(1u<<i))!=0;
        next.blocked_buttons[i]&=held;
        if(racing(next,i) && (held&VB_PAD_START) && !(previous&VB_PAD_START))next.menu_mask^=1u<<i;
        if(was_open || (next.menu_mask&(1u<<i)))next.blocked_buttons[i]|=held&~VB_PAD_START;
        if(racing(next,i) && ((inputs[i].command&ZrInputForfeit)
            || (was_open && (held&VB_PAD_SELECT) && !(previous&VB_PAD_SELECT)))) {
            next.result[i].status=ZrRacerStatus::Forfeit;next.menu_mask&=~(1u<<i);
        }
    }
    if(settle(next)){output.results=true;state=next;events=output;return true;}
    if(next.phase==ZrRacePhase::Countdown) {
        if(--next.countdown==0){next.phase=ZrRacePhase::Racing;next.physics_phase=0;}
        state=next;events=output;return true;
    }
    ++next.race_fields;
    if(next.physics_phase==0) {
        output.physics=true;next.world.random=next.world.random*0x5d588b65u+1;
        std::array<bool,ZrMaxPlayers> crossed{};
        for(unsigned i=0;i<next.players;++i)if(racing(next,i)) {
            const uint16_t pad=(next.menu_mask&(1u<<i))?0:uint16_t(inputs[i].pad&~(VB_PAD_START|next.blocked_buttons[i]));
            if(!ZrActorHumanStep(next.actors[i],rom,size,pad,next.controls[i],output.human[i],native)
               || !ZrActorProgress(next.actors[i],next.world,rom,size,crossed[i],native))return false;
        }
        unsigned pair=0;
        for(unsigned i=0;i<next.players;++i)for(unsigned j=i+1;j<next.players;++j,++pair)
            if(racing(next,i) && racing(next,j)
               && !ZrActorContact(next.actors[i],i,next.actors[j],j,next.world,rom,size,output.contact[pair],native))return false;
        for(unsigned i=0;i<next.players;++i)if(racing(next,i)) {
            if(!ZrActorEnvironment(ZrEnvironment::Surface,next.actors[i],next.world,i,rom,size,output.surface[i],native)
               || !ZrActorEnvironment(ZrEnvironment::Wall,next.actors[i],next.world,i,rom,size,output.wall[i],native)
               || !ZrActorEnvironment(ZrEnvironment::Repair,next.actors[i],next.world,i,rom,size,output.repair[i],native))return false;
            // Versus records the finish on its crossing field, using the stock
            // terminal counter, before a same-field crash can replace it.
            if(crossed[i] && word(next.actors[i].body.data()+0x48)==5) {
                next.result[i]={ZrRacerStatus::Finished,next.race_fields};
            } else if(word(next.actors[i].body.data()+0x38)==800)next.result[i].status=ZrRacerStatus::Crashed;
            if(!racing(next,i))next.menu_mask&=~(1u<<i);
        }
        ZrWorldAdvanceEffects(next.world);ZrWorldSyncRacerModels(next.world,next.actors);
        output.results=settle(next);
    }
    next.physics_phase=(next.physics_phase+1)%3;
    if(!ZrSessionValidate(next,rom,size))return false;
    state=next;events=output;return true;
}
