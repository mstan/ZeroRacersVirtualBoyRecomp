#include "zero_racers_menu.h"
#include "input.h"
bool ZrMenuValidate(const ZrMenuState& m) {
    if(m.screen>2 || m.highlight>1 || m.players<2 || m.players>4 || m.course<1 || m.course>16
       || m.row>m.players+2 || m.ready_mask>>m.players || m.pending_launch>1 || m.native_launch>1)return false;
    for(unsigned i=0;i<4;++i)if(m.machines[i]>=4 || m.previous[i]>65535)return false;
    return true;
}
void ZrMenuStep(ZrMenuState& m,const std::array<uint16_t,4>& pads) {
    std::array<uint16_t,4> pressed{};
    for(unsigned i=0;i<4;++i){pressed[i]=uint16_t(pads[i]&~m.previous[i]);m.previous[i]=pads[i];}
    if(m.screen!=2 || m.pending_launch)return;
    if(pressed[0]&VB_PAD_B){m.screen=1;m.ready_mask=0;return;}
    if(pressed[0]&VB_PAD_LUP)m.row=m.row?m.row-1:m.players+2;
    if(pressed[0]&VB_PAD_LDOWN)m.row=(m.row+1)%(m.players+3);
    const int change=(pressed[0]&VB_PAD_LRIGHT?1:0)-(pressed[0]&VB_PAD_LLEFT?1:0);
    auto cycle=[](unsigned value,int delta,unsigned count){return unsigned((int(value)+delta+int(count))%int(count));};
    if(change) {
        if(m.row==0){m.players=2+cycle(m.players-2,change,3);m.ready_mask=0;}
        else if(m.row==1){m.course=1+cycle(m.course-1,change,16);m.ready_mask=0;}
        else if(m.row<m.players+2){const unsigned seat=m.row-2;m.machines[seat]=cycle(m.machines[seat],change,4);m.ready_mask&=~(1u<<seat);}
    }
    for(unsigned i=1;i<m.players;++i) {
        const int delta=(pressed[i]&VB_PAD_LRIGHT?1:0)-(pressed[i]&VB_PAD_LLEFT?1:0);
        if(delta){m.machines[i]=cycle(m.machines[i],delta,4);m.ready_mask&=~(1u<<i);}
        if(pressed[i]&VB_PAD_A)m.ready_mask^=1u<<i;
        if(pressed[i]&VB_PAD_B)m.ready_mask&=~(1u<<i);
    }
    if((pressed[0]&VB_PAD_A) && m.row==2)m.ready_mask^=1;
    if((pressed[0]&VB_PAD_START) || ((pressed[0]&VB_PAD_A) && m.row==m.players+2)) {
        m.ready_mask|=1;
        if(m.ready_mask==((1u<<m.players)-1))m.pending_launch=1;
    }
}
