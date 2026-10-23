#pragma once
#include "zero_racers_world.h"

// The cartridge calculates this after preparing a view, for its next update.
// Each human needs a separate latch; neither bias changes actor physics.
struct ZrWarningState {
    uint32_t previous_damage=0,model_bias=0,display_bias=0;
    std::array<uint8_t,32> power_world{};
};
static_assert(sizeof(ZrWarningState)==44,"Canonical warning layout");
bool ZrActorWarning(ZrWarningState&,const ZrActorState&,uint32_t native_frame,
                     const uint8_t* rom,size_t,ZrEnvironmentEvents&,ZrNativeCall native=nullptr);
bool ZrWarningValidate(const ZrWarningState&);
