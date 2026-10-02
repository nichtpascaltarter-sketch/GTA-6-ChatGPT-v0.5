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
    float textWidth(const char* text,float size) const {return float(std::strlen(text))*size*6;}
    void text(float x,float y,const char* str,float size,Vec3 color={.94f,.96f,.94f},float alpha=1) {
        float start=x;
        for(const char* s=str;*s;++s){if(*s=='\n'){x=start;y+=size*10;continue;}const auto* g=glyph(*s);
            for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(g[row]&(1<<(4-col)))rect(x+col*size,y+row*size,size,size,color,alpha);
            x+=size*6;
        }
    }
    void wrapped(float x,float y,const char* str,float size,float maxWidth,Vec3 color={.8f,.84f,.84f}) {
        float cursor=x;const char* p=str;
        while(*p){const char* end=p;while(*end&&*end!=' '&&*end!='\n')++end;float w=float(end-p)*size*6;
            if(cursor>x&&cursor+w>x+maxWidth){cursor=x;y+=size*10;}
            char word[256];size_t n=std::min(size_t(end-p),sizeof(word)-1);std::memcpy(word,p,n);word[n]=0;text(cursor,y,word,size,color);cursor+=w+size*6;
            if(*end=='\n'){cursor=x;y+=size*10;}p=*end?end+1:end;
        }
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

inline void drawHud(Ui& ui,const Game& g,float fps,bool diagnostics,const Renderer& renderer) {
    const float s=ui.scale,margin=30*s;char b[160];const Vec3 teal{.31f,.88f,.77f},gold{1,.74f,.36f},muted{.65f,.72f,.74f};
    ui.rect(margin,margin,5*s,39*s,teal);ui.text(margin+18*s,margin,g.world.district(g.player),2.5f*s);
    int hour=int(g.dayTime),minute=int((g.dayTime-hour)*60);
    std::snprintf(b,sizeof(b),"MERIDIAN COAST  /  %02d:%02d",hour,minute);ui.text(margin+18*s,margin+26*s,b,1.4f*s,muted);
    std::snprintf(b,sizeof(b),"$%06d",g.money);ui.text(ui.width-margin-ui.textWidth(b,3*s),margin,b,3*s,teal);
    for(int i=0;i<5;++i)ui.text(ui.width-margin-(5-i)*19*s,margin+37*s,"*",2.2f*s,i<g.wanted?gold:Vec3{.25f,.29f,.31f});
    const float map=186*s,mx=margin,my=ui.height-margin-map;
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
    Vec3 objective=g.missionTarget();float cx=mx+map*.5f,cy=my+map*.5f;
    float ox=(objective.x-g.player.x)*map/(range*2),oy=-(objective.z-g.player.z)*map/(range*2);
    float edge=std::max(std::fabs(ox),std::fabs(oy)),limit=map*.5f-9*s;
    if(edge>limit){ox*=limit/edge;oy*=limit/edge;float l=std::sqrt(ox*ox+oy*oy),dx=ox/l,dy=oy/l;
        ui.triangle({cx+ox+dx*5*s,cy+oy+dy*5*s},{cx+ox-dx*5*s-dy*4*s,cy+oy-dy*5*s+dx*4*s},{cx+ox-dx*5*s+dy*4*s,cy+oy-dy*5*s-dx*4*s},gold);
    }else marker(objective,gold,4);
    float a=g.yaw;ui.triangle({cx+std::sin(a)*8*s,cy-std::cos(a)*8*s},{cx+std::sin(a+2.5f)*7*s,cy-std::cos(a+2.5f)*7*s},{cx+std::sin(a-2.5f)*7*s,cy-std::cos(a-2.5f)*7*s},teal);
    ui.text(mx+7*s,my+7*s,"N",1.5f*s);ui.rect(mx,my+map+8*s,map,5*s,{.11f,.2f,.2f});ui.rect(mx,my+map+8*s,map*clamp(g.health/100,0,1),5*s,teal);
    const Mission* mission=g.missionInfo();float tx=mx+map+24*s,ty=ui.height-margin-64*s;
    ui.text(tx,ty,g.activeMission>=0?"ACTIVE CONTRACT":"AVAILABLE CONTRACT",1.4f*s,gold);
    if(mission){ui.text(tx,ty+19*s,mission->title,2.1f*s);float dist=length(objective-g.player);std::snprintf(b,sizeof(b),"%.0f M  /  %s",dist,g.activeMission>=0?"FOLLOW THE GOLD MARKER":"M TO ACCEPT AT THE MARKER");ui.text(tx,ty+43*s,b,1.3f*s,muted);}
    if(g.occupied>=0&&g.occupied<int(g.vehicles.size())){std::snprintf(b,sizeof(b),"%03d",int(std::fabs(g.vehicles[size_t(g.occupied)].speed)*3.6f));ui.text(ui.width-margin-102*s,ui.height-margin-60*s,b,5*s);ui.text(ui.width-margin-71*s,ui.height-margin-17*s,"KM/H",1.6f*s,muted);}
    else {std::snprintf(b,sizeof(b),"%02d / %03d",g.ammo,g.reserveAmmo);ui.text(ui.width-margin-ui.textWidth(b,2.2f*s),ui.height-margin-20*s,b,2.2f*s);}
    if(g.messageTime>0&&!g.message.empty()){float w=std::min(ui.width-80*s,720*s);ui.rect((ui.width-w)*.5f,110*s,w,92*s,{.025f,.045f,.06f},.92f);ui.rect((ui.width-w)*.5f,110*s,4*s,92*s,gold);ui.wrapped((ui.width-w)*.5f+20*s,127*s,g.message.c_str(),1.8f*s,w-40*s);}
    if(g.occupied<0){float x=ui.width*.5f,y=ui.height*.5f;ui.line(x-8*s,y,x-3*s,y,s,{1,1,1},.75f);ui.line(x+3*s,y,x+8*s,y,s,{1,1,1},.75f);ui.line(x,y-8*s,x,y-3*s,s,{1,1,1},.75f);ui.line(x,y+3*s,x,y+8*s,s,{1,1,1},.75f);}
    const char* radio[]={"RADIO OFF","TIDELINE FM","NIGHT WINDOW","ION DRIVE"};ui.text(ui.width-margin-260*s,88*s,radio[std::clamp(g.radioStation,0,3)],1.4f*s,muted);
    if(diagnostics){std::snprintf(b,sizeof(b),"%.1f FPS  /  %llu FRAMES  /  %s",fps,static_cast<unsigned long long>(renderer.frameCount()),renderer.rayTracingAvailable()?"DXR AVAILABLE":"RASTER");ui.rect(0,ui.height-16*s,ui.width,16*s,{0,0,0},.8f);ui.text(5*s,ui.height-13*s,b,1.25f*s,teal);}
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
