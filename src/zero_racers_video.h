#pragma once
#include "zero_racers_session.h"
#include "zero_racers_menu.h"
#include "zero_racers_warning.h"
bool ZrVideoInitialize(const uint8_t*,size_t);
void ZrVideoInvalidate();
void ZrVideoCacheEnable(bool);
int ZrVideoMenu(const ZrMenuState&,int eye,uint32_t* out);
int ZrVideoSize(const ZrSessionState&,int seat,int* width,int* height);
int ZrVideoRender(const ZrSessionState&,const std::array<ZrCameraState,4>&,
                  const std::array<ZrWarningState,4>&,
                  const uint8_t*,size_t,ZrNativeCall,int seat,int eye,uint32_t*,int width,int height);
