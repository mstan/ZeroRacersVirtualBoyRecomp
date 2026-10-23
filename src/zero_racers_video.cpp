#include "zero_racers_video.h"
#include "zero_racers_font.h"
#include "renderer.h"
#include "vip.h"
#include "red_lut.h"
#include "netplay.h"
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <memory>

namespace {
ZrFont font;
struct ViewCache {
    bool valid=false;
    ZrWorldState world;
    std::array<ZrActorState,6> actors;
    ZrCameraState camera;
    int32_t model_bias=0;
    ZrEyePlanes planes;
    ZrEyeTags tags;
    ZrEyeRacerBounds racers;
};
std::array<ViewCache,4> cache;
// The exact cache is a qualified-correctness experiment. Keep the portable
// rebuild path as default until balanced CPU measurements establish a benefit.
bool cache_enabled=false;
struct Frame {
    std::array<uint32_t,VB_RENDER_PIXELS> stock;
    std::array<uint8_t,VB_RENDER_PIXELS> levels;
    std::array<VbSourceTexel,VB_RENDER_PIXELS> sources;
};
Frame frame;
std::array<uint32_t,VB_RENDER_PIXELS> pane;
uint32_t word(const uint8_t* p){return p[0]|uint32_t(p[1])<<8|uint32_t(p[2])<<16|uint32_t(p[3])<<24;}
void text(Frame& f,int x,int y,const char* label,uint32_t ink=2) {
    for(const unsigned char* p=reinterpret_cast<const unsigned char*>(label);*p;++p,x+=8) {
        unsigned tile=*p;
        for(unsigned v=0;v<8;++v)for(unsigned u=0;u<8;++u) {
            const int px=x+int(u),py=y+int(v);
            if(px<0 || px>=384 || py<0 || py>=224)continue;
            const unsigned raw=(font[tile*16+v*2+u/4]>>(u%4*2))&3;
            if(!raw)continue;
            const unsigned i=py*384+px;f.levels[i]=raw;
            auto& source=f.sources[i];source={};source.kind=4;source.tile_hash=ink;source.raw=raw;
        }
    }
}
void player_tag(Frame& f,const ZrRacerBounds& bounds,unsigned seat) {
    // Attach to visible native wireframe pixels, after each eye's projection,
    // clipping and first-writer occlusion. Hidden ships have no floating tag.
    if(bounds.pixels<3)return;
    const int x=(bounds.left+bounds.right)/2-8,y=bounds.top-11;
    if(x<1 || x+16>=384 || y<18 || y+8>=198)return;
    const unsigned char label[3]={'P',static_cast<unsigned char>('1'+seat),0};
    // A one-pixel silhouette keeps the native 8x8 race font legible over
    // tunnel lines, with transparent gaps instead of an opaque nameplate.
    for(unsigned n=0;n<2;++n)for(unsigned v=0;v<8;++v)for(unsigned u=0;u<8;++u) {
        if(!((font[label[n]*16+v*2+u/4]>>(u%4*2))&3))continue;
        for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx) {
            const unsigned at=(y+int(v)+dy)*384+x+int(n*8+u)+dx;
            f.levels[at]=0;f.sources[at]={};
        }
    }
    static constexpr unsigned ink[4]={3,6,11,7}; // cyan, yellow, violet, green
    text(f,x,y,reinterpret_cast<const char*>(label),ink[seat]);
}
// The title uses cartridge artwork, not the common 8x8 HUD font. Borrow its
// M/O/D/E and arrows directly from each eye's native BG map. Read on every
// presentation so CHR/map writes, reset and restore cannot leave stale art.
unsigned title_texel(int eye,unsigned x,unsigned y) {
    const unsigned cell=vb_vip_read16(0x20000+(6+eye)*8192+(y/8*64+x/8)*2);
    unsigned u=x%8,v=y%8;
    if(cell&0x2000)u=7-u;
    if(cell&0x1000)v=7-v;
    return (vb_vip_read16(0x78000+(cell&2047)*16+v*2)>>(u*2))&3;
}
void title_pixel(unsigned x,unsigned y,unsigned raw,unsigned palette,unsigned ink) {
    if(!raw)return;
    const unsigned i=y*384+x,level=(palette>>(raw*2))&3;
    frame.levels[i]=level;
    auto& source=frame.sources[i];source={};source.kind=4;source.tile_hash=ink;source.raw=raw;
}
void title_versus(int eye,unsigned y,bool selected,unsigned palette,unsigned arrow_palette) {
    // V/S are the two missing letters in the title's baked word artwork.
    // Match its 8x16 condensed stems and two-level beveled shading; the other
    // four letters and selection arrows are the cartridge's actual pixels.
    static constexpr const char* v[16]={
        "122...12","122...12","122...12","122...12",
        "122...12","122...12","122..122",".221.122",
        ".221.122",".221122.",".221122.","..22122.",
        "..22122.","...222..","...122..","........"};
    static constexpr const char* s[16]={
        ".112222.","1122.122","122...12","122...12",
        "122.....","122.....",".221....","..22221.",
        "...11222",".....122","......12","122...12",
        "122..122",".221122.","..2222..","........"};
    for(unsigned row=0;row<16;++row)for(unsigned x=0;x<8;++x) {
        title_pixel(161+x,y+row,v[row][x]=='.'?0:v[row][x]-'0',palette,2);
        title_pixel(170+x,y+row,s[row][x]=='.'?0:s[row][x]-'0',palette,2);
        title_pixel(188+x,y+row,title_texel(eye,167+x,144+row),palette,2);
        title_pixel(197+x,y+row,title_texel(eye,192+x,168+row),palette,2);
        title_pixel(206+x,y+row,title_texel(eye,210+x,168+row),palette,2);
        title_pixel(215+x,y+row,title_texel(eye,174+x,168+row),palette,2);
        if(selected) {
            title_pixel(145+x,y+row,title_texel(eye,132+x,96+row),arrow_palette,4);
            title_pixel(231+x,y+row,title_texel(eye,242+x,96+row),arrow_palette,4);
        }
    }
}
void clock(char* out,size_t capacity,unsigned fields) {
    // The stock timer advances two centiseconds per display field.
    const uint64_t centiseconds=uint64_t(fields)*2;
    std::snprintf(out,capacity,"%02u:%02u:%02u",unsigned(centiseconds/6000),unsigned(centiseconds/100%60),unsigned(centiseconds%100));
}
const char* status(ZrRacerStatus s) {
    switch(s){case ZrRacerStatus::Finished:return "FINISHED";case ZrRacerStatus::Crashed:return "CRASHED";
    case ZrRacerStatus::Forfeit:return "RETIRED";case ZrRacerStatus::Survived:return "WINNER";default:return "RACING";}
}
void standings(Frame& f,const ZrSessionState& session) {
    f.levels.fill(0);f.sources.fill({});text(f,112,32,session.phase==ZrRacePhase::Results?"VS RESULTS":"STANDINGS",4);
    const auto order=ZrSessionStandings(session);
    for(unsigned row=0;row<session.players;++row) {
        const unsigned i=order.seats[row];
        char line[48],time[32];clock(time,sizeof(time),session.result[i].finish_field);
        if(order.places[i])std::snprintf(line,sizeof(line),"%u  P%u %s",order.places[i],i+1,status(session.result[i].status));
        else std::snprintf(line,sizeof(line),"-  P%u %s",i+1,status(session.result[i].status));
        text(f,32,64+row*32,line);
        text(f,264,64+row*32,session.result[i].status==ZrRacerStatus::Finished?time:"--:--:--");
    }
    if(session.phase==ZrRacePhase::Results) {
        text(f,64,192,"START:REMATCH  B:CHANGE RACE",4);
        text(f,144,208,vb_netplay_enabled()?"SELECT:LOBBY":"SELECT:TITLE",4);
    }
}
bool draw(const ZrSessionState& session,const std::array<ZrCameraState,4>& cameras,
          const std::array<ZrWarningState,4>& warnings,
          const uint8_t* rom,size_t size,ZrNativeCall native,unsigned seat,int eye,uint32_t* out) {
    auto& saved=cache[seat];
    const int32_t bias=session.result[seat].status==ZrRacerStatus::Racing?int32_t(warnings[seat].display_bias):0;
    if(!cache_enabled || !saved.valid || std::memcmp(&saved.world,&session.world,sizeof(saved.world))
       || std::memcmp(&saved.actors,&session.actors,sizeof(saved.actors))
       || std::memcmp(&saved.camera,&cameras[seat],sizeof(saved.camera)) || saved.model_bias!=bias) {
        auto view=std::make_unique<ZrViewGeometry>();
        ZrEyePlanes planes{};auto tags=std::make_unique<ZrEyeTags>();
        ZrEyeRacerBounds racers;
        if(!ZrCameraBuildFromPose(*view,cameras[seat],session.world,session.actors,seat,rom,size,native,bias,session.players)
           || !ZrRasterGeometry(*view,planes,rom,size,native,tags.get(),&racers))return false;
        saved.world=session.world;saved.actors=session.actors;saved.camera=cameras[seat];saved.model_bias=bias;
        saved.planes=planes;saved.tags=*tags;saved.racers=racers;saved.valid=true;
    }
    frame.sources.fill({});
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<384;++x) {
        const unsigned i=y*384+x;const uint8_t level=(saved.planes[eye][x*64+y/4]>>(y%4*2))&3;
        frame.levels[i]=level;
        auto& source=frame.sources[i];source.kind=4;source.map=255;source.x=x;source.y=y;
        source.raw=level;source.tile_hash=saved.tags[eye][i];
    }
    for(unsigned racer=0;racer<session.players;++racer) {
        const auto status=session.result[racer].status;
        if(status==ZrRacerStatus::Racing || status==ZrRacerStatus::Finished || status==ZrRacerStatus::Survived)
            player_tag(frame,saved.racers[eye][racer],racer);
    }
    char label[64],time[32];const auto* body=session.actors[seat].body.data();
    const unsigned speed=word(body+0x34);
    const unsigned display_speed=speed<1600?(speed+15)/16:speed<3200?100+(speed-1600)*700/1600:speed/4;
    std::snprintf(label,sizeof(label),"P%u  LAP %u/5",seat+1,std::min(5u,word(body+0x48)+1));text(frame,8,8,label);
    clock(time,sizeof(time),ZrSessionClock(session,seat));text(frame,304,8,time);
    std::snprintf(label,sizeof(label),"%03u KM/H",std::min(display_speed,999u));text(frame,280,200,label,3);
    std::snprintf(label,sizeof(label),"POWER %03u",800-std::min(800u,word(body+0x38)));text(frame,8,200,label,7);
    if(session.phase==ZrRacePhase::Countdown) {
        const unsigned elapsed=ZrCountdownFields-session.countdown;
        if(elapsed>=ZrCountdownBeat && elapsed%ZrCountdownBeat<ZrCountdownVisible) {
            std::snprintf(label,sizeof(label),"%u",4-elapsed/ZrCountdownBeat);text(frame,184,72,label,4);
        }
    }
    if(session.phase==ZrRacePhase::Racing && session.race_fields<ZrCountdownVisible)text(frame,176,72,"GO!",4);
    if(session.result[seat].status!=ZrRacerStatus::Racing)text(frame,144,64,status(session.result[seat].status),4);
    if(session.menu_mask&(1u<<seat)) {
        text(frame,112,96,"RACE CONTINUES",4);text(frame,112,112,"START  RESUME");text(frame,112,128,"SELECT  RETIRE");
    }
    if(session.phase==ZrRacePhase::Results)standings(frame,session);
    uint32_t colors[96][4];vb_vip_column_argb(eye,colors);
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<384;++x) {
        const unsigned i=y*384+x;frame.stock[i]=vb_red_lut_map(int((colors[x/4][frame.levels[i]]>>16)&255));
    }
    const VbRenderFrame source{session.tick,eye,frame.stock.data(),frame.levels.data(),nullptr,frame.sources.data(),384,224};
    return vb_renderer_apply_frame(&source,out)!=0;
}
}
bool ZrVideoInitialize(const uint8_t* rom,size_t size) {ZrVideoInvalidate();return ZrDecodeFont(font,rom,size);}
void ZrVideoInvalidate(){for(auto& c:cache)c.valid=false;}
void ZrVideoCacheEnable(bool enabled){cache_enabled=enabled;ZrVideoInvalidate();}
int ZrVideoMenu(const ZrMenuState& menu,int eye,uint32_t* out) {
    if(!out || !menu.screen || eye<0 || eye>1)return 0;
    frame.levels.fill(0);frame.sources.fill({});
    if(menu.screen==1) {
        vb_renderer_present(eye,out);
        std::copy_n(out,VB_RENDER_PIXELS,pane.data());
        // The cartridge alternates the selected word's map cells between
        // GPLT1 and GPLT0 every eight menu iterations (21F7A..22014).
        // Use those stable palettes with VS's own selection/phase; borrowing
        // the displayed TIME TRIAL pixels couples VS to that word's blink.
        const unsigned normal=vb_vip_read16(0x5f860);
        const unsigned selected=vb_vip_read16(0x5f862);
        // Five native-height rows fit between the logo and copyright with a
        // six-pixel gap. Preserve the original artwork and selected-row blink.
        std::fill(out+88*384,out+204*384,0xff000000u);
        for(unsigned row=0;row<4;++row)for(unsigned y=0;y<16;++y)
            for(unsigned x=0;x<384;++x) {
                if(menu.highlight && row==3)continue;
                out[(92+row*22+y)*384+x]=pane[(96+row*24+y)*384+x];
            }
        if(menu.highlight)for(unsigned y=0;y<16;++y)for(unsigned x=160;x<224;++x)
            title_pixel(x,158+y,title_texel(eye,x,168+y),normal,2);
        title_versus(eye,180,menu.highlight!=0,
                     menu.highlight && menu.title_phase<8?selected:normal,normal);
    } else {
        std::fill_n(out,VB_RENDER_PIXELS,0xff000000u);text(frame,152,16,"VS MODE",4);
        char label[64];
        std::snprintf(label,sizeof(label),"%c PLAYERS    %u",menu.row==0?'>':' ',menu.players);text(frame,80,40,label);
        std::snprintf(label,sizeof(label),"%c COURSE     %02u",menu.row==1?'>':' ',menu.course);text(frame,80,56,label);
        for(unsigned i=0;i<menu.players;++i) {
            std::snprintf(label,sizeof(label),"%c P%u MACHINE %u   %s",menu.row==i+2?'>':' ',i+1,menu.machines[i]+1,
                menu.ready_mask&(1u<<i)?"READY":"A:READY");text(frame,80,80+i*20,label);
        }
        text(frame,80,168,menu.row==menu.players+2?"> START RACE":"  START RACE",4);
        text(frame,64,192,"LEFT/RIGHT:CHANGE  B:BACK");text(frame,64,208,"EACH PLAYER A:READY");
    }
    uint32_t colors[96][4];vb_vip_column_argb(eye,colors);
    for(unsigned y=0;y<224;++y)for(unsigned x=0;x<384;++x) {
        const unsigned i=y*384+x;frame.stock[i]=colors[x/4][frame.levels[i]];
    }
    const VbRenderFrame source{0,eye,frame.stock.data(),frame.levels.data(),nullptr,frame.sources.data(),384,224};
    if(!vb_renderer_apply_frame(&source,pane.data()))return 0;
    for(unsigned i=0;i<VB_RENDER_PIXELS;++i)if(frame.levels[i])out[i]=pane[i];
    return 1;
}
int ZrVideoSize(const ZrSessionState& s,int seat,int* width,int* height) {
    if(s.phase==ZrRacePhase::Empty || s.phase==ZrRacePhase::Ready || !width || !height || seat<-1 || seat>=int(s.players))return 0;
    if(s.phase==ZrRacePhase::Results){*width=384;*height=224;return 1;}
    *width=seat>=0 || s.players==2?384:768;*height=seat>=0?224:448;return 1;
}
int ZrVideoRender(const ZrSessionState& s,const std::array<ZrCameraState,4>& cameras,
                  const std::array<ZrWarningState,4>& warnings,
                  const uint8_t* rom,size_t size,ZrNativeCall native,int seat,int eye,uint32_t* out,int width,int height) {
    int w,h;if(!out || !ZrVideoSize(s,seat,&w,&h) || width!=w || height!=h || eye<0 || eye>1)return 0;
    if(seat>=0)return draw(s,cameras,warnings,rom,size,native,unsigned(seat),eye,out);
    if(s.phase==ZrRacePhase::Results)return draw(s,cameras,warnings,rom,size,native,0,eye,out);
    std::fill_n(out,size_t(w)*h,0xff000000u);
    for(unsigned i=0;i<s.players;++i) {
        if(!draw(s,cameras,warnings,rom,size,native,i,eye,pane.data()))return 0;
        const unsigned x=s.players==2?0:(i%2)*384,y=s.players==2?i*224:(i/2)*224;
        for(unsigned row=0;row<224;++row)std::copy_n(pane.data()+row*384,384,out+(y+row)*w+x);
    }
    if(s.players==3) {
        standings(frame,s);uint32_t colors[96][4];vb_vip_column_argb(eye,colors);
        for(unsigned y=0;y<224;++y)for(unsigned x=0;x<384;++x)frame.stock[y*384+x]=vb_red_lut_map(int((colors[x/4][frame.levels[y*384+x]]>>16)&255));
        const VbRenderFrame source{s.tick,eye,frame.stock.data(),frame.levels.data(),nullptr,frame.sources.data(),384,224};
        if(!vb_renderer_apply_frame(&source,pane.data()))return 0;
        for(unsigned y=0;y<224;++y)std::copy_n(pane.data()+y*384,384,out+(y+224)*768+384);
    }
    return 1;
}
