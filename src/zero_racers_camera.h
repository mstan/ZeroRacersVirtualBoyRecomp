#pragma once
#include "zero_racers_world.h"

struct ZrCameraState {
    uint32_t mode=0;
    std::array<uint8_t,16> smoothing{}; // native 10B0..10BC
    std::array<uint8_t,24> pose{};      // native position + Euler angles
};
// Derived, view-owned geometry. Never part of the authoritative race digest.
// Kept separate from the canonical actors/world; all world mappings are RO.
struct ZrViewGeometry {
    ZrCameraState camera;
    uint32_t near_sector=0,far_sector=0,edges=0,vertices=0,lines=0;
    std::array<uint8_t,1026*12> vertex{};
    std::array<uint8_t,514*8> edge{};
    std::array<uint8_t,1026*32> line{};
};
enum class ZrCameraStage { Pose, Sector, Models, Tunnel, Project, Clip };
bool ZrCameraBuildStage(ZrCameraStage,ZrViewGeometry&,const ZrWorldState&,
                        const std::array<ZrActorState,6>&,unsigned slot,
                        const uint8_t* rom,size_t,ZrNativeCall native=nullptr);
bool ZrCameraBuild(ZrViewGeometry&,const ZrWorldState&,
                   const std::array<ZrActorState,6>&,unsigned slot,
                   const uint8_t* rom,size_t,ZrNativeCall native=nullptr);
using ZrEyePlanes=std::array<std::array<uint8_t,0x6000>,2>;
// Original first-writer line rasterizer. Both eye planes are independent.
// Caller owns the initial pixels (e.g. native HUD); no implicit clear/VRAM.
bool ZrRasterGeometry(const ZrViewGeometry&,ZrEyePlanes&,const uint8_t* rom,size_t,
                       ZrNativeCall native=nullptr);
