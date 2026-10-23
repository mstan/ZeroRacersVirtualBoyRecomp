#include "zero_racers_bus.h"

namespace {constexpr uint32_t stack_base=0x0500fe00u,stack_end=0x05010000u;}
bool ZrMappedBus::map(uint32_t address,void* bytes,size_t size,bool writable) {
    if(!constant(address,bytes,size))return false;
    if(writable)regions_[count_-1].write=static_cast<uint8_t*>(bytes);
    return true;
}
bool ZrMappedBus::track(uint32_t address,void* bytes,size_t size,uint32_t& dirty,uint32_t bit) {
    if(!bit || !map(address,bytes,size))return false;
    regions_[count_-1].dirty=&dirty;regions_[count_-1].bit=bit;return true;
}
bool ZrMappedBus::constant_word(uint32_t address,uint32_t value) {
    if(constants_count_==constants_.size())return valid_=false;
    auto& bytes=constants_[constants_count_++];
    for(unsigned i=0;i<4;++i)bytes[i]=uint8_t(value>>(i*8));
    return constant(address,bytes.data(),bytes.size());
}
bool ZrMappedBus::word(uint32_t address,uint32_t& value) {
    if(!map(address,&value,sizeof(value)))return false;
    regions_[count_-1].scalar=&value;return true;
}
bool ZrMappedBus::rom_word(uint32_t address,uint32_t value) {
    address&=0x07ffffffu;
    if(address<0x07000000 || address>0x07fffffc || (address&3)
       || rom_words_count_==rom_words_.size())return valid_=false;
    for(size_t i=0;i<rom_words_count_;++i)if(rom_words_[i].address==address)return valid_=false;
    rom_words_[rom_words_count_++]={address,value};return true;
}
bool ZrMappedBus::track_word(uint32_t address,uint32_t& value,uint32_t& dirty,uint32_t bit) {
    if(!bit || !word(address,value))return false;
    regions_[count_-1].dirty=&dirty;regions_[count_-1].bit=bit;return true;
}
bool ZrMappedBus::constant(uint32_t address,const void* bytes,size_t size) {
    address&=0x07ffffffu;
    if(!bytes || !size || count_==regions_.size() || size>0x08000000u-address
       || address>=0x07000000u || (address<stack_end && address+size>stack_base))
        return valid_=false;
    for(size_t i=0;i<count_;++i)
        if(address<regions_[i].address+regions_[i].size && address+size>regions_[i].address)
            return valid_=false;
    regions_[count_++]={address,size,static_cast<const uint8_t*>(bytes),nullptr,nullptr,0};
    return true;
}
bool ZrMappedBus::read(void* context,uint32_t address,unsigned width,uint32_t* value) {
    auto& b=*static_cast<ZrMappedBus*>(context);address&=0x07ffffffu;
    if(!b.valid_ || (width!=1 && width!=2 && width!=4) || width>0x08000000u-address)return false;
    if(address>=0x07000000u) {
        if(!b.rom_ || !b.rom_size_ || (b.rom_size_&(b.rom_size_-1)))return false;
        *value=0;for(unsigned i=0;i<width;++i) {
            uint8_t byte=b.rom_[(address+i)&(b.rom_size_-1)];
            for(size_t j=0;j<b.rom_words_count_;++j) {
                const auto& r=b.rom_words_[j];
                if(address+i>=r.address && address+i-r.address<4){byte=uint8_t(r.value>>((address+i-r.address)*8));break;}
            }
            *value|=uint32_t(byte)<<(8*i);
        }
        return true;
    }
    if(address>=stack_base && address+width<=stack_end) {
        const auto offset=address-stack_base;
        for(unsigned i=0;i<width;++i)if(!b.stack_written_[offset+i])return false;
        *value=0;for(unsigned i=0;i<width;++i)*value|=uint32_t(b.stack_[offset+i])<<(8*i);
        return true;
    }
    for(size_t i=0;i<b.count_;++i) {
        const auto& r=b.regions_[i];
        if(address>=r.address && address-r.address<=r.size && width<=r.size-(address-r.address)) {
            *value=0;
            for(unsigned j=0;j<width;++j) {
                const auto offset=address-r.address+j;
                const uint8_t byte=r.scalar?uint8_t(*r.scalar>>(offset*8)):r.read[offset];
                *value|=uint32_t(byte)<<(8*j);
            }
            return true;
        }
    }
    return false;
}
bool ZrMappedBus::write(void* context,uint32_t address,unsigned width,uint32_t value) {
    auto& b=*static_cast<ZrMappedBus*>(context);address&=0x07ffffffu;
    if(!b.valid_ || (width!=1 && width!=2 && width!=4) || width>0x08000000u-address)return false;
    if(address>=stack_base && address+width<=stack_end) {
        const auto offset=address-stack_base;
        for(unsigned i=0;i<width;++i){b.stack_[offset+i]=uint8_t(value>>(8*i));b.stack_written_.set(offset+i);}
        return true;
    }
    for(size_t i=0;i<b.count_;++i) {
        auto& r=b.regions_[i];
        if(r.write && address>=r.address && address-r.address<=r.size && width<=r.size-(address-r.address)) {
            if(b.observer_)b.observer_(b.cpu_,address,width,value,b.observer_context_);
            for(unsigned j=0;j<width;++j) {
                const auto offset=address-r.address+j;const uint8_t byte=uint8_t(value>>(8*j));
                if(r.scalar)*r.scalar=(*r.scalar&~(0xffu<<(offset*8)))|(uint32_t(byte)<<(offset*8));
                else r.write[offset]=byte;
            }
            if(r.dirty)*r.dirty|=r.bit;
            return true;
        }
    }
    return false;
}
