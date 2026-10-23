#include "zero_racers_font.h"
#include "zero_racers_bus.h"
#include <algorithm>
#include <cstdio>
bool ZrDecodeFont(ZrFont& font,const uint8_t* rom,size_t size,ZrNativeCall native) {
    if(!rom || size!=1048576)return false;
    std::array<uint8_t,32768> output{};
    std::array<uint8_t,1024> dictionary{};
    std::array<uint8_t,16> globals{};
    ZrMappedBus bus(rom,size);
    bus.map(0x78000,output.data(),output.size());
    bus.map(0x05000c00,dictionary.data(),dictionary.size());
    bus.map(0x05001014,globals.data(),globals.size());
    CPUState cpu{};cpu.gpr[3]=0x0500fffc;cpu.gpr[4]=0x05008000;
    cpu.gpr[6]=0xfff28de0;cpu.gpr[7]=0x78000;
    const auto result=ZrRoutineRun(cpu,bus.callbacks(),0xfff0e40e,0xfff0ff84,8000000,native);
    const uint32_t end=globals[4]|uint32_t(globals[5])<<8|uint32_t(globals[6])<<16|uint32_t(globals[7])<<24;
    // The stock stream emits three trailing bytes into the next CHR bank;
    // race setup immediately replaces that bank with the following asset.
    if(!result.completed || end!=0x78000+font.size()+3) {
        std::fprintf(stderr,"Stock font decode failed: completed=%u end=%08x pc=%08x address=%08x cycles=%llu\n",
            unsigned(result.completed),end,result.failure_pc,result.failure_address,(unsigned long long)result.cycles);
        return false;
    }
    std::copy_n(output.begin(),font.size(),font.begin());return true;
}
