#ifndef ZERO_RACERS_MATERIAL_COLORS_H
#define ZERO_RACERS_MATERIAL_COLORS_H
#include <array>
#include <algorithm>
#include <cstdint>
#include <unordered_map>

namespace zr_color {
using Mask=std::array<uint8_t,64>;
using Materials=std::unordered_map<uint64_t,Mask>;
template<unsigned InkCount> class PreparedColors {
    struct Entry {uint64_t key{};const Mask* mask{};bool valid{};};
    std::array<Entry,4096> cache{};
    std::array<std::array<uint32_t,256>,InkCount> colors{};
    std::array<bool,InkCount> visible{};
public:
    void rebuild(const std::array<unsigned,InkCount>& palette) {
        cache.fill({});
        for(unsigned ink=0;ink<InkCount;++ink) {
            const unsigned rgb=palette[ink],r=(rgb>>16)&255,g=(rgb>>8)&255,b=rgb&255;
            const unsigned peak=std::max({r,g,b});
            visible[ink]=ink!=0 && peak!=0;
            for(unsigned light=0;light<256;++light)
                colors[ink][light]=peak ? 0xff000000u|((r*light/peak)<<16)
                    |((g*light/peak)<<8)|(b*light/peak) : 0;
        }
    }
    const Mask* material(uint64_t key,const Materials& materials) {
        auto& entry=cache[((key^(key>>32))*0x9e3779b1u)&(cache.size()-1)];
        if(!entry.valid || entry.key!=key) {
            auto found=materials.find(key);
            entry={key,found==materials.end()?nullptr:&found->second,true};
        }
        return entry.mask;
    }
    void apply(unsigned ink,uint8_t light,uint32_t& out) const {
        if(!light || ink>=InkCount || !visible[ink])return;
        out=colors[ink][light];
    }
};
}
#endif
