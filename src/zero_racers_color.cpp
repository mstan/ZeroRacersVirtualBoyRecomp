// Presentation only: all geometry, clipping, occlusion and brightness remain native.
#include "renderer.h"
#include "mod_runtime.h"
#include "memory.h"
#include "cpu_state.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>

namespace {
enum Ink : unsigned { Original, Track, White, Cyan, Selection, Blue, Yellow, Green, Red, Amber, Teal, Violet, InkCount };
constexpr uint32_t ModelTag = 0x10000;
const char* ink_names[] = {"original", "track", "white", "cyan", "selection",
                          "blue", "yellow", "green", "red", "amber", "teal", "violet"};
std::array<unsigned, InkCount> palette;
std::array<unsigned, 2048> model_inks;
std::array<uint32_t, 514> edge_tags{};
std::array<uint32_t, 1026> line_tags{};
using Mask = std::array<uint8_t, 64>;
std::unordered_map<uint64_t, Mask> materials;

uint64_t material_key(unsigned map, unsigned kind, unsigned x, unsigned y, uint32_t hash) {
    return hash | (uint64_t(x) << 32) | (uint64_t(y) << 42)
        | (uint64_t(map) << 52) | (uint64_t(kind) << 60);
}

uint32_t model_tag(uint32_t descriptor) {
    descriptor &= 0xfffff;
    // Preserve identity rather than the currently selected color. Palette or
    // package changes while paused can recolor the already displayed models.
    if (descriptor < 0x61080 || descriptor >= 0x68010 || (descriptor & 15)) return Original;
    return ModelTag + (descriptor - 0x61080) / 16;
}

uint32_t observe_store(const CPUState* cpu, uint32_t address, uint32_t, unsigned width) {
    const uint32_t pc = cpu->pc & 0xfffff;
    const uint32_t phys = address & VB_PHYS_MASK;
    if (((phys >> 24) & 7) == 5) {
        const unsigned off = phys & 65535;
        if (off >= 0x4220 && off < 0x5230 && ((off - 0x4220) & 7) == 0 && width == 2) {
            // Tag edge creation, before projection. Later clipping edits the
            // same endpoints; it must retain the originating model's identity.
            if (pc == 0x1403c) edge_tags[(off - 0x4220) / 8] = model_tag(cpu->gpr[6]);
            else if (pc >= 0x1420e && pc < 0x14960) edge_tags[(off - 0x4220) / 8] = Track;
        }
        if (width == 4 && (pc == 0x15012 || pc == 0x15150) && off >= 0x5230
            && ((off - 0x5230) & 31) == 0 && (off - 0x5230) / 32 < line_tags.size()) {
            const unsigned edge = cpu->gpr[25] - 0x05004220u;
            line_tags[(off - 0x5230) / 32] = edge < edge_tags.size() * 8 && !(edge & 7)
                ? edge_tags[edge / 8] : Original;
        }
        return Original;
    }
    if ((phys >> 24) == 0 && (pc >= 0x13b5a && pc <= 0x13ccc)) {
        const unsigned line = cpu->gpr[17] - 0x05005230u;
        if (line < line_tags.size() * 32 && !(line & 31)) return line_tags[line / 32];
    }
    return Original;
}

bool has_race_hud(const VbRenderFrame* frame) {
    // The POWER label is an invariant source-art anchor. Do not infer the
    // screen from moving world numbers, elapsed time, or mutable guest RAM.
    for (unsigned i = 0; i < VB_RENDER_PIXELS; ++i) {
        const auto& s = frame->sources[i];
        if (s.world && s.kind == 0 && s.map == 1 && s.x / 8 == 4 && s.y / 8 == 23
            && s.tile_hash == 0x944f03df) return true;
    }
    return false;
}

unsigned hud_ink(const VbSourceTexel& s, unsigned authored) {
    // These are source-atlas regions, not destination/screen coordinates.
    // Dynamic digits, counter states and rotating minimap texels use the same
    // roles even when their CHR content was absent from the reference capture.
    if (s.map == 2 && s.kind == 2) return Track;
    if (s.map == 1 && s.kind == 0) {
        if (s.x >= 24 && s.x < 48 && s.y >= 192 && s.y < 208) return Yellow;
        if (s.x < 24 && s.y >= 98 && s.y < 160) return Cyan;
        if (s.x >= 160 && s.x < 176 && s.y >= 120 && s.y < 200) return Green;
        if (s.x >= 256 && s.x < 336 && s.y >= 80 && s.y < 192) return Cyan;
    }
    if (s.map == 4 && s.kind == 0 && s.x < 96 && s.y < 40) {
        if (s.x < 2 || s.x >= 94 || s.y < 2 || s.y >= 38) return Cyan;
        if (s.y >= 24 && s.y < 32) return Selection;
        return White;
    }
    return authored;
}

void render(const VbRenderFrame* frame, uint32_t* out, void*) {
    if (!frame->sources) return;
    const bool race = has_race_hud(frame);
    for (unsigned i = 0; i < VB_RENDER_PIXELS; ++i) {
        const uint32_t native = frame->stock_argb[i];
        // Consult the actual native color, including brightness-register fades.
        // Never add a pixel in a black gap, even if source metadata is present.
        const unsigned light = (native >> 16) & 255;
        if (!light) continue;
        const auto& source = frame->sources[i];
        unsigned ink = White;
        if (source.kind == 4) {
            const uint32_t tag = source.tile_hash;
            ink = tag >= ModelTag && tag - ModelTag < model_inks.size()
                ? model_inks[tag - ModelTag] : tag > Original && tag < InkCount ? tag : Track;
        }
        else if (source.world && source.kind <= 3) {
            auto found = materials.find(material_key(source.map, source.kind,
                source.x / 8, source.y / 8, source.tile_hash));
            if (found != materials.end() && found->second[source.v * 8 + source.u])
                ink = found->second[source.v * 8 + source.u];
            if (race) ink = hud_ink(source, ink);
        }
        if (ink == Original || ink >= InkCount) continue;
        const unsigned rgb = palette[ink];
        const unsigned r = (rgb >> 16) & 255, g = (rgb >> 8) & 255, b = rgb & 255;
        const unsigned peak = std::max({r, g, b});
        if (!peak) continue;
        // Normalize hue so the native intensity remains the maximum channel.
        // Even the dimmest visible line stays visible with custom palettes.
        out[i] = 0xff000000u | ((r * light / peak) << 16)
            | ((g * light / peak) << 8) | (b * light / peak);
    }
}

void activate() {
    palette = {0xff0000, 0xddeaff, 0xf2f6ff, 0x66ddff, 0xffd65c,
               0x599aff, 0xffe36a, 0x70ffa0, 0xff615c, 0xffac55, 0x67ffdd, 0xc493ff};
    model_inks.fill(Track);
    // Older 0.1.0 archives predate models.txt; retain their four ship colors.
    const unsigned original_models[] = {Red, Red, Blue, Blue, Yellow, Yellow, Green, Green};
    std::copy(std::begin(original_models), std::end(original_models), model_inks.begin() + 256);
    materials.clear();
    char path[1024];
    if (vb_mod_asset("palette.txt", path, sizeof(path))) {
        std::ifstream input(path); std::string name; unsigned color;
        while (input >> name >> std::hex >> color) {
            if (color > 0xffffff) continue;
            for (unsigned n = 1; n < InkCount; ++n) if (name == ink_names[n]) palette[n] = color;
        }
    }
    if (vb_mod_asset("models.txt", path, sizeof(path))) {
        std::ifstream input(path); unsigned first, last; std::string name;
        while (input >> first >> last >> name) {
            if (first > last || last >= model_inks.size()) continue;
            for (unsigned n = 1; n < InkCount; ++n) if (name == ink_names[n])
                std::fill(model_inks.begin() + first, model_inks.begin() + last + 1, n);
        }
    }
    if (vb_mod_option("track_tone", path, sizeof(path)) && std::string(path) == "neutral")
        palette[Track] = 0xffffff;
    if (vb_mod_asset("materials.txt", path, sizeof(path))) {
        std::ifstream input(path); unsigned map, kind, x, y, hash; std::string mask;
        while (input >> std::dec >> map >> kind >> x >> y >> std::hex >> hash >> mask) {
            if (map > 255 || kind > 3 || x > 1023 || y > 1023 || mask.size() != 64) continue;
            Mask values{}; bool valid = true;
            for (unsigned i = 0; i < 64; ++i) {
                const int digit = mask[i] >= '0' && mask[i] <= '9' ? mask[i] - '0'
                    : mask[i] >= 'a' && mask[i] <= 'f' ? mask[i] - 'a' + 10 : -1;
                if (digit < 0 || digit >= int(InkCount)) {valid = false; break;}
                values[i] = uint8_t(digit);
            }
            if (valid) materials[material_key(map, kind, x, y, hash)] = values;
        }
    }
    vb_renderer_register("zero-racers.full-color", render, nullptr);
}

VB_MOD_CONSTRUCTOR(register_color) {
    vb_renderer_track_texels();
    vb_memory_register_write_observer(observe_store);
    vb_mod_register_exclusive_plugin("zero-racers.full-color", "video.renderer", activate);
}
}
