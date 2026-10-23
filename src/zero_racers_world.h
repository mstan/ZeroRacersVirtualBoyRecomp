#pragma once
#include "zero_racers_actor.h"

struct ZrWorldState {
    uint32_t course=0;
    uint32_t random=0;
    uint32_t native_10fc=0; // original selected human model identity, not difficulty
    // Stock model instances: racers 0..5, collision sparks 6..11,
    // repair 12..15, authored effects 16..18, course objects 19..71.
    std::array<uint8_t,72*32> models{};
};
struct ZrEnvironmentEvents {
    uint32_t written=0; // bits 0..3: 1160,E2,F1,F2, retaining explicit zero commands
    uint32_t native_1160=0;
    uint8_t native_e2=0,native_f1=0,native_f2=0;
};
// All calls are transactional: a rejected/unknown bus access changes nothing.
bool ZrActorProgress(ZrActorState&,const ZrWorldState&,const uint8_t* rom,size_t,
                     bool& crossed,ZrNativeCall native=nullptr);
enum class ZrEnvironment { Wall, Surface, Repair };
bool ZrActorEnvironment(ZrEnvironment,ZrActorState&,ZrWorldState&,unsigned slot,
                        const uint8_t* rom,size_t,ZrEnvironmentEvents&,
                        ZrNativeCall native=nullptr);
// Original swept human detector + original two-body response, exactly once
// for the selected unordered pair. Caller supplies a stable pair order.
bool ZrActorContact(ZrActorState& first,unsigned first_slot,ZrActorState& second,
                    unsigned second_slot,ZrWorldState&,const uint8_t* rom,size_t,
                    ZrEnvironmentEvents&,ZrNativeCall native=nullptr);
