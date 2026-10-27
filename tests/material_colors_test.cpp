#include "material_colors.h"
#include <cassert>
#include <cstdio>

static uint32_t scalar(unsigned ink,unsigned light,uint32_t native,
                       const std::array<unsigned,12>& palette) {
    if(!light || ink==0 || ink>=palette.size())return native;
    const unsigned r=palette[ink]>>16&255,g=palette[ink]>>8&255,b=palette[ink]&255;
    const unsigned peak=std::max({r,g,b});if(!peak)return native;
    return 0xff000000u|((r*light/peak)<<16)|((g*light/peak)<<8)|b*light/peak;
}
int main() {
    zr_color::PreparedColors<12> prepared;
    std::array<unsigned,12> palette{};
    unsigned comparisons=0;
    for(unsigned scene=0;scene<256;++scene) {
        for(unsigned ink=0;ink<palette.size();++ink)
            palette[ink]=(scene*2654435761u+ink*2246822519u)&0xffffff;
        palette[scene%palette.size()]=0;
        prepared.rebuild(palette);
        for(unsigned ink=0;ink<16;++ink)for(unsigned light=0;light<256;++light) {
            const uint32_t native=(light<<16)|0x7f0055aa;
            uint32_t actual=native;prepared.apply(ink,uint8_t(light),actual);
            assert(actual==scalar(ink,light,native,palette));++comparisons;
        }
    }
    zr_color::Materials materials;
    // Keys differing by a cache period collide, but only a complete key hits.
    for(unsigned i=0;i<4096;++i) {
        const uint64_t key=uint64_t(i)*4096;
        auto& mask=materials[key];mask.fill(uint8_t(i%12));
    }
    prepared.rebuild(palette);
    for(unsigned turn=0;turn<3;++turn)for(unsigned i=0;i<8192;++i) {
        const uint64_t key=uint64_t(i)*4096;
        auto expected=materials.find(key);const auto* actual=prepared.material(key,materials);
        assert(actual==(expected==materials.end()?nullptr:&expected->second));++comparisons;
    }
    materials.clear();materials[0].fill(7);prepared.rebuild(palette);
    assert(prepared.material(0,materials)==&materials.at(0));
    assert(prepared.material(0,materials)->at(63)==7);
    assert(!prepared.material(4096,materials));
    std::printf("%u scalar palette/material comparisons passed; service %zu bytes\n",comparisons,sizeof(prepared));
}
