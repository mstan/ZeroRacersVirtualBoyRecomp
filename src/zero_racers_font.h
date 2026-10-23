#pragma once
#include "zero_racers_routine.h"
#include <array>
using ZrFont=std::array<uint8_t,8192>;
// Decode the cartridge's common CHR atlas through its original bounded LZSS
// routine. The atlas is presentation-only and never copied from another racer.
bool ZrDecodeFont(ZrFont&,const uint8_t* rom,size_t,ZrNativeCall native=nullptr);
