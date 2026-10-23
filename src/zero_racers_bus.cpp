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
        *value=0;for(unsigned i=0;i<width;++i)*value|=uint32_t(b.rom_[(address+i)&(b.rom_size_-1)])<<(8*i);
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
            *value=0;for(unsigned j=0;j<width;++j)*value|=uint32_t(r.read[address-r.address+j])<<(8*j);
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
            for(unsigned j=0;j<width;++j)r.write[address-r.address+j]=uint8_t(value>>(8*j));
            if(r.dirty)*r.dirty|=r.bit;
            return true;
        }
    }
    return false;
}
