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
    ZrRoutineBus callbacks(){return {this,read,write};}
    bool valid() const {return valid_;}
private:
    struct Region {uint32_t address;size_t size;const uint8_t* read;uint8_t* write;uint32_t* dirty;uint32_t bit;};
    std::array<Region,32> regions_{};
    size_t count_=0;
    const uint8_t* rom_;
    size_t rom_size_;
    bool valid_=true;
    std::array<uint8_t,512> stack_{};
    std::bitset<512> stack_written_;
    static bool read(void*,uint32_t,unsigned,uint32_t*);
    static bool write(void*,uint32_t,unsigned,uint32_t);
};
