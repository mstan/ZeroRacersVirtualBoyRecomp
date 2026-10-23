#include "zero_racers_actor.h"
#include "input.h"
#include <bitset>

namespace {
constexpr uint32_t actor_base=0x0500db70u,stack_base=0x0500fe00u;
struct Bus {
    ZrActorState actor;
    ZrActorControls controls;
    ZrActorEvents events{};
    uint32_t input;
    const uint8_t* rom;
    size_t rom_size;
    std::array<uint8_t,512> stack{};
    std::bitset<512> stack_written;
    uint8_t* byte(uint32_t address,bool writing) {
        if(address>=actor_base && address<actor_base+actor.body.size())
            return &actor.body[address-actor_base];
        if(address>=stack_base && address<stack_base+stack.size()) {
            const unsigned i=address-stack_base;
            if(!writing && !stack_written[i])return nullptr;
            if(writing)stack_written.set(i);
            return &stack[i];
        }
        return nullptr;
    }
    uint32_t* word(uint32_t address,bool writing) {
        switch(address) {
        case 0x05001024:return &input;
        case 0x0500106c:return writing?nullptr:&controls.native_106c;
        case 0x05001070:return writing?nullptr:&controls.native_1070;
        case 0x05001140:return &actor.native_1140;
        case 0x05001144:return &actor.native_1144;
        case 0x05001160:if(writing)events.written|=1;return writing?&events.native_1160:nullptr;
        case 0x0500116c:if(writing)events.written|=2;return writing?&events.native_116c:nullptr;
        default:return nullptr;
        }
    }
    static bool read(void* context,uint32_t address,unsigned width,uint32_t* result) {
        auto& b=*static_cast<Bus*>(context);address&=0x07ffffffu;
        if(address>>24==7) {
            *result=0;for(unsigned i=0;i<width;++i)
                *result|=uint32_t(b.rom[(address+i)&(b.rom_size-1)])<<(i*8);
            return true;
        }
        if(width==4)if(const auto* w=b.word(address,false)){*result=*w;return true;}
        *result=0;
        for(unsigned i=0;i<width;++i) {
            const auto* p=b.byte(address+i,false);if(!p)return false;
            *result|=uint32_t(*p)<<(i*8);
        }
        return true;
    }
    static bool write(void* context,uint32_t address,unsigned width,uint32_t value) {
        auto& b=*static_cast<Bus*>(context);address&=0x07ffffffu;
        if(width==4)if(auto* w=b.word(address,true)){*w=value;return true;}
        if(width==1) {
            switch(address) {
            case 0x050000ee:b.events.native_ee=(uint8_t)value;b.events.written|=4;return true;
            case 0x050000f1:b.events.native_f1=(uint8_t)value;b.events.written|=8;return true;
            case 0x050000f2:b.events.native_f2=(uint8_t)value;b.events.written|=16;return true;
            case 0x050000f3:b.events.native_f3=(uint8_t)value;b.events.written|=32;return true;
            default:break;
            }
        }
        for(unsigned i=0;i<width;++i) {
            auto* p=b.byte(address+i,true);if(!p)return false;
            *p=(uint8_t)(value>>(i*8));
        }
        return true;
    }
};
}
bool ZrActorHumanStep(ZrActorState& actor,const uint8_t* rom,size_t rom_size,
                      uint16_t held,ZrActorControls controls,ZrActorEvents& events,
                      ZrNativeCall native) {
    if(!rom || rom_size!=1048576)return false;
    held=(uint16_t)((held&~VB_PAD_BAT_LOW)|VB_PAD_PRESENT);
    // Original 13922 packs newly pressed bits above the current pad word.
    const uint32_t input=held|(uint32_t(held&~actor.previous_buttons)<<16);
    Bus bus{actor,controls,{},input,rom,rom_size,{}, {}};
    CPUState cpu{};cpu.gpr[3]=0x0500fffcu;cpu.gpr[4]=0x05008000u;cpu.gpr[6]=actor_base;
    const auto result=ZrRoutineRun(cpu,{&bus,Bus::read,Bus::write},
                                  0xfff151c4u,0xfff0f53cu,200000,native);
    if(!result.completed)return false;
    actor=bus.actor;actor.previous_buttons=held;events=bus.events;return true;
}
