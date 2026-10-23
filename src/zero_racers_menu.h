#pragma once
#include <array>
#include <cstdint>
// Canonical front-end choices and button edges. Host devices only provide
// sealed pads; restores/rollback replay these choices through the same path.
struct ZrMenuState {
    uint32_t screen=0,highlight=0,row=0,players=2,course=1;
    std::array<unsigned,4> machines{0,1,2,3};
    uint32_t ready_mask=0,pending_launch=0,native_launch=0;
    std::array<uint32_t,4> previous{};
};
bool ZrMenuValidate(const ZrMenuState&);
void ZrMenuStep(ZrMenuState&,const std::array<uint16_t,4>&,unsigned authority=0,bool fixed_players=false,bool allow_back=true);
