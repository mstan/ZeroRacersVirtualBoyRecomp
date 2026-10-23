#pragma once
#include "zero_racers_routine.h"
#include <array>
#include <bitset>

// A checked address view for one bounded original-ROM call. Every region has
// an explicit owner and access policy. There is no fallback to the live bus.
class ZrMappedBus {
public:
    ZrMappedBus(const uint8_t* rom,size_t size):rom_(rom),rom_size_(size){}
    bool map(uint32_t address,void* bytes,size_t size,bool writable=true);
    bool constant(uint32_t address,const void* bytes,size_t size);
    bool track(uint32_t address,void* bytes,size_t size,uint32_t& dirty,uint32_t bit);
    // Numeric host fields use explicit guest little-endian byte order. Raw
    // byte spans above already contain the cartridge's little-endian layout.
    bool constant_word(uint32_t address,uint32_t value);
    // A call-owned immutable header override, used only for a derived draw
    // list's count. It cannot authorize writes to the cartridge.
    bool rom_word(uint32_t address,uint32_t value);
    bool word(uint32_t address,uint32_t& value);
    bool track_word(uint32_t address,uint32_t& value,uint32_t& dirty,uint32_t bit);
    using StoreObserver=void (*)(const CPUState*,uint32_t,unsigned,uint32_t,void*);
    void observe(CPUState* cpu,StoreObserver callback,void* context) {cpu_=cpu;observer_=callback;observer_context_=context;}
    ZrRoutineBus callbacks(){return {this,read,write};}
    bool valid() const {return valid_;}
private:
    struct Region {uint32_t address;size_t size;const uint8_t* read;uint8_t* write;uint32_t* dirty;uint32_t bit;uint32_t* scalar=nullptr;};
    std::array<Region,32> regions_{};
    size_t count_=0;
    std::array<std::array<uint8_t,4>,32> constants_{};
    size_t constants_count_=0;
    struct RomWord {uint32_t address,value;};
    std::array<RomWord,4> rom_words_{};
    size_t rom_words_count_=0;
    const uint8_t* rom_;
    size_t rom_size_;
    bool valid_=true;
    CPUState* cpu_=nullptr;
    StoreObserver observer_=nullptr;
    void* observer_context_=nullptr;
    std::array<uint8_t,512> stack_{};
    std::bitset<512> stack_written_;
    static bool read(void*,uint32_t,unsigned,uint32_t*);
    static bool write(void*,uint32_t,unsigned,uint32_t);
};
