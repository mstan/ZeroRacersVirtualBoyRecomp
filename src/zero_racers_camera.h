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
    std::array<uint32_t,514> edge_tags{};
    std::array<uint32_t,1026> line_tags{};
    // Stable racer ownership, independent of machine/descriptor choice.
    // Zero is scenery; 1..4 are the human model slots before projection.
    std::array<uint8_t,514> edge_racers{};
    std::array<uint8_t,1026> line_racers{};
};
enum class ZrCameraStage { Pose, Sector, Models, Tunnel, Project, Clip };
struct ZrModelDisplayList {uint32_t count=0;std::array<uint8_t,84*32> models{};};
bool ZrComposeRepairModels(ZrModelDisplayList&,const ZrWorldState&,
                            const std::array<ZrActorState,6>&,unsigned players,const uint8_t*,size_t);
// Advance the logical camera only at the fixed simulation cadence. View
// generation below consumes that frozen pose and may repeat without moving it.
bool ZrCameraUpdatePose(ZrCameraState&,const std::array<ZrActorState,6>&,unsigned slot,
                        const uint8_t* rom,size_t,ZrNativeCall native=nullptr);
// Original view-dependent tunnel-cap/display-list preparation. This copy is
// owned by one view; choosing its near/far sectors never mutates shared world.
bool ZrCameraPrepareWorld(ZrWorldState& output,const ZrWorldState&,uint32_t near_sector,
                          uint32_t far_sector,const uint8_t* rom,size_t,
                          ZrNativeCall native=nullptr);
bool ZrCameraBuildStage(ZrCameraStage,ZrViewGeometry&,const ZrWorldState&,
                        const std::array<ZrActorState,6>&,unsigned slot,
                        const uint8_t* rom,size_t,ZrNativeCall native=nullptr,const ZrModelDisplayList* list=nullptr);
bool ZrCameraBuild(ZrViewGeometry&,const ZrWorldState&,
                   const std::array<ZrActorState,6>&,unsigned slot,
                   const uint8_t* rom,size_t,ZrNativeCall native=nullptr);
bool ZrCameraBuildFromPose(ZrViewGeometry&,const ZrCameraState&,const ZrWorldState&,
                           const std::array<ZrActorState,6>&,unsigned slot,
                           const uint8_t* rom,size_t,ZrNativeCall native=nullptr,int32_t own_model_bias=0,unsigned repair_players=0);
using ZrEyePlanes=std::array<std::array<uint8_t,0x6000>,2>;
using ZrEyeTags=std::array<std::array<uint32_t,384*224>,2>;
struct ZrRacerBounds {
    int left=384,top=224,right=-1,bottom=-1;
    unsigned pixels=0;
};
using ZrEyeRacerBounds=std::array<std::array<ZrRacerBounds,4>,2>;
// Original first-writer line rasterizer. Both eye planes are independent.
// Caller owns the initial pixels (e.g. native HUD); no implicit clear/VRAM.
bool ZrRasterGeometry(const ZrViewGeometry&,ZrEyePlanes&,const uint8_t* rom,size_t,
                       ZrNativeCall native=nullptr,ZrEyeTags* tags=nullptr,
                       ZrEyeRacerBounds* racers=nullptr);
