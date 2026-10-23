#pragma once
#include "zero_racers_routine.h"
#include <array>

/* Verified stock racer record: six records begin at WRAM DB70, stride A0.
 * Keep the original little-endian representation until individual fields have
 * been mapped; no unrelated player-zero RAM is copied into an actor. */
struct ZrActorState {
    std::array<uint8_t,0xa0> body{};
    uint32_t native_1140=0, native_1144=0;
    uint32_t native_1104=0, native_1148=0;
    std::array<uint8_t,4*32> repair_models{};
    uint16_t previous_buttons=0;
    uint16_t menu_buttons=0;
};
struct ZrActorControls {
    uint32_t native_106c=0, native_1070=0;
};
/* These destinations are write-only in the human routine. The shared audio
 * owner consumes the emitted commands once; they are not host playback state. */
struct ZrActorEvents {
    uint32_t written=0; // bits 0..5: 1160,116C,EE,F1,F2,F3 (including zero writes)
    uint32_t native_1160=0, native_116c=0;
    uint8_t native_ee=0, native_f1=0, native_f2=0, native_f3=0;
};
bool ZrActorHumanStep(ZrActorState& actor,const uint8_t* rom,size_t rom_size,
                      uint16_t held,ZrActorControls controls,ZrActorEvents& events,
                      ZrNativeCall native=nullptr);
