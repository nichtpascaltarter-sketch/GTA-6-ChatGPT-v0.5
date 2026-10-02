#pragma once
#include "renderer.h"
#include "game.h"
#include "cinematics.h"
#include <cstdio>
#include <cstring>

namespace mc {
class Ui {
public:
    std::vector<UiVertex> vertices;
    float width=1280,height=720,scale=1;
    void begin(float w,float h) {width=w;height=h;scale=std::max(.65f,h/900.f);vertices.clear();}
    void rect(float x,float y,float w,float h,Vec3 color,float alpha=1) {
        UiVertex a{x,y,color.x,color.y,color.z,alpha},b{x+w,y,color.x,color.y,color.z,alpha};
        UiVertex c{x+w,y+h,color.x,color.y,color.z,alpha},d{x,y+h,color.x,color.y,color.z,alpha};
        vertices.insert(vertices.end(),{a,b,c,a,c,d});
    }
    void line(float x,float y,float xx,float yy,float thickness,Vec3 color,float alpha=1) {
        float dx=xx-x,dy=yy-y,l=std::sqrt(dx*dx+dy*dy);if(l<.001f)return;
        dx*=thickness*.5f/l;dy*=thickness*.5f/l;
        UiVertex a{x-dy,y+dx,color.x,color.y,color.z,alpha},b{xx-dy,yy+dx,color.x,color.y,color.z,alpha};
        UiVertex c{xx+dy,yy-dx,color.x,color.y,color.z,alpha},d{x+dy,y-dx,color.x,color.y,color.z,alpha};
        vertices.insert(vertices.end(),{a,b,c,a,c,d});
    }
    void triangle(Vec2 a,Vec2 b,Vec2 c,Vec3 color,float alpha=1) {
        vertices.insert(vertices.end(),{{a.x,a.y,color.x,color.y,color.z,alpha},{b.x,b.y,color.x,color.y,color.z,alpha},{c.x,c.y,color.x,color.y,color.z,alpha}});
    }
    void circle(float x,float y,float radius,Vec3 color,float alpha=1) {
        for(int i=0;i<24;++i){float a=i*Pi/12,b=(i+1)*Pi/12;triangle({x,y},{x+std::cos(a)*radius,y+std::sin(a)*radius},{x+std::cos(b)*radius,y+std::sin(b)*radius},color,alpha);}
    }
    float textWidth(const char* text,float size) const {return float(std::strlen(text))*std::max(1.f,size)*6;}
    void text(float x,float y,const char* str,float size,Vec3 color={.94f,.96f,.94f},float alpha=1) {
        // Every authored bitmap cell needs at least one physical pixel. Smaller
        // rectangles dropped entire rows at the minimum supported window size.
        size=std::max(1.f,size);
        float start=x;
        for(const char* s=str;*s;++s){if(*s=='\n'){x=start;y+=size*10;continue;}const auto* g=glyph(*s);
            for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(g[row]&(1<<(4-col)))rect(x+col*size,y+row*size,size,size,color,alpha);
            x+=size*6;
        }
    }
    void wrapped(float x,float y,const char* str,float size,float maxWidth,Vec3 color={.8f,.84f,.84f}) {
        size=std::max(1.f,size);
        float cursor=x;const char* p=str;
        while(*p){const char* end=p;while(*end&&*end!=' '&&*end!='\n')++end;float w=float(end-p)*size*6;
            if(cursor>x&&cursor+w>x+maxWidth){cursor=x;y+=size*10;}
            char word[256];size_t n=std::min(size_t(end-p),sizeof(word)-1);std::memcpy(word,p,n);word[n]=0;text(cursor,y,word,size,color);cursor+=w+size*6;
            if(*end=='\n'){cursor=x;y+=size*10;}p=*end?end+1:end;
        }
    }
    void shadowText(float x,float y,const char* str,float size,Vec3 color={.94f,.96f,.94f}) {
        const float offset=std::max(1.f,size*.65f);
        text(x+offset,y+offset,str,size,{.005f,.01f,.015f},.9f);text(x,y,str,size,color);
    }
private:
    static const unsigned char* glyph(char c) {
        // Original 5x7 bitmap alphabet; each row is a five-bit mask.
        static constexpr unsigned char data[][7]={
            {0,0,0,0,0,0,0}, {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
            {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},{2,6,10,18,31,2,2},
            {31,16,16,30,1,1,30},{14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
            {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
            {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},{14,17,16,16,16,17,14},
            {30,17,17,17,17,17,30},{31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
            {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},{14,4,4,4,4,4,14},
            {7,2,2,2,2,18,12},{17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
            {17,27,21,21,17,17,17},{17,25,25,21,19,19,17},{14,17,17,17,17,17,14},
            {30,17,17,30,16,16,16},{14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
            {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},{17,17,17,17,17,17,14},
            {17,17,17,17,17,10,4},{17,17,17,21,21,27,17},{17,17,10,4,10,17,17},
            {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
            {0,0,0,31,0,0,0},{0,4,4,31,4,4,0},{0,0,0,0,0,6,6},
            {0,6,6,0,6,6,0},{1,1,2,4,8,16,16},{4,15,20,14,5,30,4},
            {4,4,4,4,4,0,4},{14,17,1,2,4,0,4},{4,8,16,16,16,8,4},
            {4,2,1,1,1,2,4},{0,4,2,31,2,4,0},{0,4,14,31,14,4,0},
            {4,4,0,0,0,0,0},{0,0,0,0,0,4,8},{0,0,0,0,0,0,31},
            {31,16,16,16,16,16,31},{31,1,1,1,1,1,31},{0,17,10,4,10,17,0}
        };
        if(c>='a'&&c<='z')c=char(c-'a'+'A');
        if(c>='0'&&c<='9')return data[1+c-'0'];
        if(c>='A'&&c<='Z')return data[11+c-'A'];
        const char* punctuation="-+.:/$!?()>*',_[]%";const char* found=std::strchr(punctuation,c);
        return data[found?37+size_t(found-punctuation):0];
    }
};

inline void drawHud(Ui& ui,const Game& g,float fps,bool diagnostics,const Renderer& renderer,const Vec3* waypoint=nullptr) {
    const float s=ui.scale,margin=30*s;char b[160];const Vec3 teal{.31f,.88f,.77f},gold{1,.74f,.36f},muted{.65f,.72f,.74f};
    const float contentBottom=ui.height-(diagnostics?50*s:0);
    const bool trial=g.objectiveIsTrial(),active=g.objectiveActive();
    const Vec3 violet{.75f,.43f,.95f},objectiveColor=trial?violet:gold;
    ui.rect(margin,margin,5*s,39*s,teal);ui.shadowText(margin+18*s,margin,g.world.district(g.player),2.5f*s);
    int hour=int(g.dayTime),minute=int((g.dayTime-hour)*60);
    std::snprintf(b,sizeof(b),"MERIDIAN COAST  /  %02d:%02d",hour,minute);ui.shadowText(margin+18*s,margin+26*s,b,1.4f*s,muted);
    std::snprintf(b,sizeof(b),"$%06d",g.money);ui.shadowText(ui.width-margin-ui.textWidth(b,3*s),margin,b,3*s,teal);
    for(int i=0;i<5;++i)ui.text(ui.width-margin-(5-i)*19*s,margin+37*s,"*",2.2f*s,i<g.wanted?gold:Vec3{.25f,.29f,.31f});
    const float map=186*s,mx=margin,my=contentBottom-margin-map;
    ui.rect(mx-3*s,my-3*s,map+6*s,map+6*s,{.28f,.4f,.4f},.9f);ui.rect(mx,my,map,map,{.025f,.07f,.085f},.94f);
    const float range=220,cell=map/48;
    for(int z=0;z<48;++z)for(int x=0;x<48;++x){float wx=g.player.x+(x-23.5f)*range/24,wz=g.player.z-(z-23.5f)*range/24;
        Biome biome=g.world.biome(wx,wz);Vec3 color{.13f,.2f,.18f};if(biome==Biome::Ocean)color={.04f,.17f,.23f};else if(biome==Biome::Downtown)color={.21f,.25f,.26f};else if(biome==Biome::Beach)color={.32f,.33f,.25f};
        if(g.world.road(wx,wz))color={.46f,.52f,.51f};
        ui.rect(mx+x*cell,my+z*cell,cell+.2f,cell+.2f,color);
    }
    auto marker=[&](Vec3 p,Vec3 color,float radius){float xx=mx+map*.5f+(p.x-g.player.x)*map/(range*2),yy=my+map*.5f-(p.z-g.player.z)*map/(range*2);
        if(xx>mx+5*s&&xx<mx+map-5*s&&yy>my+5*s&&yy<my+map-5*s)ui.circle(xx,yy,radius*s,color);};
    for(const auto& v:g.vehicles)marker(v.position,v.police?Vec3{.38f,.6f,1}:Vec3{.65f,.7f,.7f},v.police?2.5f:1.4f);
    marker(Game::harborSplitContact(),violet,3);
    Vec3 objective=g.objectiveTarget();float cx=mx+map*.5f,cy=my+map*.5f;
    float ox=(objective.x-g.player.x)*map/(range*2),oy=-(objective.z-g.player.z)*map/(range*2);
    float edge=std::max(std::fabs(ox),std::fabs(oy)),limit=map*.5f-9*s;
    if(edge>limit){ox*=limit/edge;oy*=limit/edge;float l=std::sqrt(ox*ox+oy*oy),dx=ox/l,dy=oy/l;
        ui.triangle({cx+ox+dx*5*s,cy+oy+dy*5*s},{cx+ox-dx*5*s-dy*4*s,cy+oy-dy*5*s+dx*4*s},{cx+ox-dx*5*s+dy*4*s,cy+oy-dy*5*s-dx*4*s},objectiveColor);
    }else marker(objective,objectiveColor,4);
    if(waypoint){float dx=(waypoint->x-g.player.x)*map/(range*2),dy=-(waypoint->z-g.player.z)*map/(range*2);
        float extent=std::max(std::fabs(dx),std::fabs(dy));if(extent>limit){dx*=limit/extent;dy*=limit/extent;}
        ui.line(cx+dx-4*s,cy+dy,cx+dx+4*s,cy+dy,2*s,teal);ui.line(cx+dx,cy+dy-4*s,cx+dx,cy+dy+4*s,2*s,teal);
        std::snprintf(b,sizeof(b),"WAYPOINT %.2f KM",std::hypot(waypoint->x-g.player.x,waypoint->z-g.player.z)*.001f);ui.shadowText(ui.width-margin-260*s,110*s,b,1.4f*s,teal);
    }
    float a=g.yaw;ui.triangle({cx+std::sin(a)*8*s,cy-std::cos(a)*8*s},{cx+std::sin(a+2.5f)*7*s,cy-std::cos(a+2.5f)*7*s},{cx+std::sin(a-2.5f)*7*s,cy-std::cos(a-2.5f)*7*s},teal);
    ui.text(mx+7*s,my+7*s,"N",1.5f*s);ui.rect(mx,my+map+8*s,map,5*s,{.11f,.2f,.2f});ui.rect(mx,my+map+8*s,map*clamp(g.health/100,0,1),5*s,teal);
    const Mission* mission=g.objectiveInfo();float tx=mx+map+24*s,ty=contentBottom-margin-64*s;
    const float remaining=g.objectiveTimeRemaining();
    if(active&&remaining>0){int seconds=int(std::ceil(remaining));std::snprintf(b,sizeof(b),"%s  /  %d:%02d",trial?"ACTIVE TRIAL":"ACTIVE CONTRACT",seconds/60,seconds%60);ui.shadowText(tx,ty,b,1.4f*s,objectiveColor);}
    else ui.shadowText(tx,ty,!mission?"EXPLORE THE COAST":active?(trial?"ACTIVE TRIAL":"ACTIVE CONTRACT"):(trial?"AVAILABLE TRIAL":"AVAILABLE CONTRACT"),1.4f*s,objectiveColor);
    if(mission){ui.shadowText(tx,ty+19*s,mission->title,2.1f*s);float dist=length(objective-g.player);std::snprintf(b,sizeof(b),"%.0f M  /  %s",dist,active||trial?g.objectiveInstruction():"M TO ACCEPT AT THE MARKER");ui.shadowText(tx,ty+43*s,b,1.3f*s,muted);}
    else {ui.shadowText(tx,ty+19*s,"THE CITY IS YOURS TO EXPLORE",1.7f*s);ui.shadowText(tx,ty+43*s,"TAB / BACK  MAP    M AT SHOPS  SERVICES",1.3f*s,muted);}
    if(g.occupied>=0&&g.occupied<int(g.vehicles.size())){
        const auto& vehicle=g.vehicles[size_t(g.occupied)];
        std::snprintf(b,sizeof(b),"%03d",int(std::fabs(vehicle.speed)*3.6f));ui.shadowText(ui.width-margin-102*s,contentBottom-margin-60*s,b,5*s);ui.shadowText(ui.width-margin-71*s,contentBottom-margin-17*s,"KM/H",1.6f*s,muted);
        if(vehicle.kind==VehicleKind::Aircraft){
            const float altitude=std::max(0.f,vehicle.position.y-g.world.height(vehicle.position.x,vehicle.position.z));
            float x=ui.width-margin-218*s,y=contentBottom-margin-260*s;
            ui.rect(x-12*s,y-12*s,230*s,162*s,{.02f,.04f,.055f},.88f);
            ui.text(x,y,"SKYLARK",1.6f*s,teal);
            std::snprintf(b,sizeof(b),"ALT GROUND  %.0f M",altitude);ui.text(x,y+30*s,b,1.45f*s);
            std::snprintf(b,sizeof(b),"THROTTLE    %d%%",int(vehicle.throttle*100));ui.text(x,y+55*s,b,1.45f*s);
            const bool stall=altitude>2&&vehicle.speed<22;
            ui.text(x,y+83*s,stall?"LOW AIRSPEED":"W/S THROTTLE  A/D BANK",1.2f*s,stall?gold:muted);
            ui.text(x,y+106*s,"SHIFT / PAD A  CLIMB",1.1f*s,muted);
            ui.text(x,y+129*s,"SPACE / PAD B  DESCEND",1.1f*s,muted);
        }else if(vehicle.kind==VehicleKind::Boat){
            float x=ui.width-margin-218*s,y=contentBottom-margin-208*s;
            ui.rect(x-12*s,y-12*s,230*s,112*s,{.02f,.04f,.055f},.88f);ui.text(x,y,"GLASSWATER RUNABOUT",1.4f*s,teal);
            std::snprintf(b,sizeof(b),"DEPTH  %.1f M",g.world.waterDepth(vehicle.position.x,vehicle.position.z));ui.text(x,y+27*s,b,1.45f*s);
            ui.text(x,y+54*s,"W/S THRUST  A/D RUDDER",1.2f*s,muted);ui.text(x,y+77*s,"SPACE / PAD B  SLOW",1.2f*s,muted);
        }
    }
    else {std::snprintf(b,sizeof(b),"%02d / %03d",g.ammo,g.reserveAmmo);ui.text(ui.width-margin-ui.textWidth(b,2.2f*s),contentBottom-margin-20*s,b,2.2f*s);}
    if(g.messageTime>0&&!g.message.empty()){float w=std::min(ui.width-80*s,720*s);ui.rect((ui.width-w)*.5f,110*s,w,92*s,{.025f,.045f,.06f},.92f);ui.rect((ui.width-w)*.5f,110*s,4*s,92*s,gold);ui.wrapped((ui.width-w)*.5f+20*s,127*s,g.message.c_str(),1.8f*s,w-40*s);}
    if(g.occupied<0){float x=ui.width*.5f,y=ui.height*.5f;ui.line(x-8*s,y,x-3*s,y,s,{1,1,1},.75f);ui.line(x+3*s,y,x+8*s,y,s,{1,1,1},.75f);ui.line(x,y-8*s,x,y-3*s,s,{1,1,1},.75f);ui.line(x,y+3*s,x,y+8*s,s,{1,1,1},.75f);}
    const char* radio[]={"RADIO OFF","TIDELINE FM","NIGHT WINDOW","ION DRIVE"};ui.shadowText(ui.width-margin-260*s,88*s,radio[std::clamp(g.radioStation,0,3)],1.4f*s,muted);
    if(diagnostics){
        const auto stats=renderer.streamStats();
        const auto timing=renderer.timingStats();
        ui.rect(0,ui.height-50*s,ui.width,50*s,{0,0,0},.8f);
        char rate[40];if(fps>0)std::snprintf(rate,sizeof(rate),"%.1f FPS",fps);else std::snprintf(rate,sizeof(rate),"FPS WARMING");
        std::snprintf(b,sizeof(b),"%s  /  %llu FRAMES  /  %s  /  VIEW %.0f M",rate,
            static_cast<unsigned long long>(renderer.frameCount()),renderer.rayTracingAvailable()?"DXR AVAILABLE":"RASTER",stats.fogEnd);
        ui.text(5*s,ui.height-46*s,b,1.25f*s,teal);
        std::snprintf(b,sizeof(b),"TILES %u / %u / %u  DRAW %u  CULLED %u  GEOMETRY %.1f MB  UPLOADS %u",
            stats.residentTilesByLod[0],stats.residentTilesByLod[1],stats.residentTilesByLod[2],stats.mainDrawn,
            stats.mainCulled,double(stats.residentBytes)/(1024*1024),stats.pendingBatches);
        ui.text(5*s,ui.height-30*s,b,1.25f*s,teal);
        if(timing.windowGpuSamples)std::snprintf(b,sizeof(b),"GPU RENDER %.2f MS MEAN / %.2f MAX  CPU RENDER %.2f MS  WAIT %.2f MS",
            timing.mean.gpuRenderMs,timing.maximum.gpuRenderMs,timing.mean.cpuRenderMs,timing.mean.cpuFenceWaitMs);
        else std::snprintf(b,sizeof(b),"%s  CPU RENDER %.2f MS  WAIT %.2f MS",timing.gpuAvailable?"GPU TIMING WARMING":"GPU TIMING UNAVAILABLE",timing.mean.cpuRenderMs,timing.mean.cpuFenceWaitMs);
        ui.text(5*s,ui.height-14*s,b,1.25f*s,teal);
    }
}
inline void drawCinematic(Ui& ui,const Cinematic& scene,const char* missionTitle) {
    float s=ui.scale;
    ui.rect(0,0,ui.width,66*s,{.005f,.009f,.012f},.96f);
    ui.rect(0,ui.height-142*s,ui.width,142*s,{.005f,.009f,.012f},.96f);
    ui.text(32*s,26*s,missionTitle?missionTitle:"MERIDIAN COAST",1.7f*s,{.85f,.91f,.9f});
    const auto* line=scene.dialogue();if(!line)return;
    float width=std::min(ui.width-80*s,940*s),left=(ui.width-width)*.5f;
    ui.text(left,ui.height-118*s,line->speaker,1.6f*s,{.31f,.88f,.77f});
    ui.wrapped(left,ui.height-91*s,line->text,2*s,width,{.93f,.95f,.93f});
    ui.text(ui.width-250*s,ui.height-22*s,"SPACE / A  SKIP SCENE",1.3f*s,{.48f,.58f,.6f});
}
}
