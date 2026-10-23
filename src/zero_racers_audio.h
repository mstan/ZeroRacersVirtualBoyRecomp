#pragma once
#include "zero_racers_camera.h"
struct ZrNearbyAudio {uint32_t speed=0,direction=0,distance=0;};
// Original ED56 nearby-engine/passing-sound calculation. Consume the selected
// actor's pass latch once; every other actor and the frozen camera stay RO.
bool ZrActorNearbyAudio(ZrActorState&,const std::array<ZrActorState,6>&,unsigned slot,
                        const ZrCameraState&,const uint8_t* rom,size_t,
                        ZrNearbyAudio&,ZrEnvironmentEvents&,ZrNativeCall native=nullptr);
