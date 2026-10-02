#pragma once
#include "ui.h"

namespace mc {
struct WorldMap {
    Vec3 center{},waypoint{};
    float span=World::Extent*2;
    bool hasWaypoint=false;
    void constrain() {
        span=clamp(span,512,World::Extent*2);
        const float edge=World::Extent-span*.5f;
        center.x=clamp(center.x,-edge,edge);center.z=clamp(center.z,-edge,edge);
    }
    void focus(Vec3 position) {center=position;constrain();}
    void navigate(float x,float y,float seconds,float zoom) {
        span*=std::pow(.75f,zoom);constrain();
        center.x+=x*span*.55f*seconds;center.z+=y*span*.55f*seconds;constrain();
    }
    struct Panel {float x,y,size;};
    static Panel panel(float width,float height,float scale) {
        float size=std::min(height-160*scale,width-340*scale);
        return {34*scale,90*scale,std::max(size,100.f)};
    }
    bool place(float x,float y,float width,float height,float scale,const World& world) {
        Panel p=panel(width,height,scale);
        if(x<p.x||y<p.y||x>p.x+p.size||y>p.y+p.size)return false;
        waypoint={center.x+((x-p.x)/p.size-.5f)*span,0,center.z+(.5f-(y-p.y)/p.size)*span};
        waypoint.y=world.height(waypoint.x,waypoint.z);hasWaypoint=true;return true;
    }
    void placeCenter(const World& world) {waypoint=center;waypoint.y=world.height(center.x,center.z);hasWaypoint=true;}
};

inline void drawWorldMap(Ui& ui,const Game& game,const WorldMap& map) {
    const float s=ui.scale;const auto p=WorldMap::panel(ui.width,ui.height,s);
    const Vec3 teal{.31f,.88f,.77f},gold{1,.74f,.36f},muted{.61f,.7f,.72f},violet{.75f,.43f,.95f};
    ui.rect(0,0,ui.width,ui.height,{.009f,.021f,.031f},.98f);
    ui.text(34*s,30*s,"MERIDIAN COAST",3.4f*s);ui.text(34*s,63*s,"WORLD MAP",1.5f*s,teal);
    ui.rect(p.x-2*s,p.y-2*s,p.size+4*s,p.size+4*s,{.24f,.37f,.4f});
    const float left=map.center.x-map.span*.5f,bottom=map.center.z-map.span*.5f;
    const float cell=p.size/128;
    for(int z=0;z<128;++z)for(int x=0;x<128;++x){
        float wx=left+(x+.5f)*map.span/128,wz=bottom+(127.5f-z)*map.span/128;
        Vec3 color;
        switch(game.world.biome(wx,wz)){
            case Biome::Downtown:color={.25f,.31f,.33f};break;
            case Biome::Residential:color={.24f,.32f,.29f};break;
            case Biome::Wetland:color={.14f,.29f,.28f};break;
            case Biome::Beach:color={.49f,.47f,.33f};break;
            case Biome::Island:color={.29f,.36f,.24f};break;
            case Biome::Ocean:color={.055f,.16f,.23f};break;
            default:color={.18f,.29f,.20f};break;
        }
        ui.rect(p.x+x*cell,p.y+z*cell,cell+.1f,cell+.1f,color);
    }
    auto project=[&](Vec3 v){return Vec2{p.x+(v.x-left)/map.span*p.size,p.y+p.size-(v.z-bottom)/map.span*p.size};};
    auto inside=[&](Vec2 v,float margin=0.f){return v.x>=p.x+margin&&v.y>=p.y+margin&&v.x<=p.x+p.size-margin&&v.y<=p.y+p.size-margin;};
    // Grid roads follow their actual rural spacing and the coastal causeway.
    const int beginX=int(std::ceil(left/128)),endX=int(std::floor((left+map.span)/128));
    const int beginZ=int(std::ceil(bottom/128)),endZ=int(std::floor((bottom+map.span)/128));
    const Vec3 roadColor{.43f,.51f,.5f};
    for(int axis=0;axis<2;++axis){
        int first=axis?beginZ:beginX,last=axis?endZ:endX;
        for(int n=first;n<=last;++n)for(float d=0;d<map.span;d+=32){
            float stop=std::min(d+32,map.span),mid=(d+stop)*.5f;
            Vec3 a=axis?Vec3{left+d,0,n*128.f}:Vec3{n*128.f,0,bottom+d};
            Vec3 b=axis?Vec3{left+stop,0,n*128.f}:Vec3{n*128.f,0,bottom+stop};
            float wx=axis?left+mid:n*128.f,wz=axis?n*128.f:bottom+mid;
            if(game.world.road(wx,wz)){Vec2 aa=project(a),bb=project(b);ui.line(aa.x,aa.y,bb.x,bb.y,std::max(.65f,p.size/map.span*7),roadColor);}
        }
    }
    // The coastal road winds between grid lines. Clip its own sampled centerline
    // to the map so it remains continuous at every zoom and pan position.
    auto clippedLine=[&](Vec3 a,Vec3 b,Vec3 color,float width){
        Vec2 aa=project(a),bb=project(b);float dx=bb.x-aa.x,dy=bb.y-aa.y,first=0,last=1;
        auto clip=[&](float direction,float distance){
            if(std::fabs(direction)<.00001f)return distance>=0;
            float t=distance/direction;if(direction<0)first=std::max(first,t);else last=std::min(last,t);return first<=last;
        };
        if(clip(-dx,aa.x-p.x)&&clip(dx,p.x+p.size-aa.x)&&clip(-dy,aa.y-p.y)&&clip(dy,p.y+p.size-aa.y))
            ui.line(aa.x+dx*first,aa.y+dy*first,aa.x+dx*last,aa.y+dy*last,width,color);
    };
    for(int part=0;part<512;++part)clippedLine(World::coastalRoadPoint(part/512.f),World::coastalRoadPoint((part+1)/512.f),roadColor,std::max(.75f,p.size/map.span*7));
    if(game.objectiveIsTrial()){
        Vec3 previous=Game::harborSplitStart();
        for(const Vec3 gate:Game::harborSplitCourse()){clippedLine(previous,gate,violet,std::max(1.0f,1.5f*s));previous=gate;}
    }
    auto dot=[&](Vec3 at,Vec3 color,float radius){Vec2 v=project(at);if(inside(v,radius*s))ui.circle(v.x,v.y,radius*s,color);};
    for(const auto& landmark:World::landmarks()){
        Vec2 at=project(landmark.position);if(!inside(at,8*s))continue;
        ui.circle(at.x,at.y,3*s,muted);
        if(map.span<=6144){float text=1.05f*s,w=ui.textWidth(landmark.name,text),x=clamp(at.x+7*s,p.x+5*s,p.x+p.size-w-5*s);
            ui.rect(x-2*s,at.y-11*s,w+4*s,11*s,{.025f,.05f,.06f},.88f);ui.text(x,at.y-9*s,landmark.name,text,muted);}
    }
    for(const auto& vehicle:game.vehicles)if(vehicle.kind==VehicleKind::Boat||vehicle.kind==VehicleKind::Aircraft)dot(vehicle.position,vehicle.kind==VehicleKind::Boat?Vec3{.42f,.72f,1}:Vec3{.88f,.83f,.97f},3);
    dot(Game::harborSplitContact(),violet,3.5f);
    dot(World::garageSite().marker,{.96f,.58f,.25f},3.5f);
    if(game.missionInfo())dot(game.missionTarget(),gold,game.objectiveIsTrial()?3.0f:5.0f);
    if(game.objectiveIsTrial())dot(game.objectiveTarget(),violet,5);
    if(map.hasWaypoint){Vec2 at=project(map.waypoint);if(inside(at,7*s)){ui.line(at.x-6*s,at.y,at.x+6*s,at.y,2*s,teal);ui.line(at.x,at.y-6*s,at.x,at.y+6*s,2*s,teal);}}
    Vec2 player=project(game.player);if(inside(player,9*s)){
        float a=game.yaw;ui.triangle({player.x+std::sin(a)*9*s,player.y-std::cos(a)*9*s},{player.x+std::sin(a+2.5f)*8*s,player.y-std::cos(a+2.5f)*8*s},{player.x+std::sin(a-2.5f)*8*s,player.y-std::cos(a-2.5f)*8*s},teal);
    }
    const float cx=p.x+p.size*.5f,cy=p.y+p.size*.5f;
    ui.line(cx-7*s,cy,cx+7*s,cy,s,{.9f,.95f,.93f},.65f);ui.line(cx,cy-7*s,cx,cy+7*s,s,{.9f,.95f,.93f},.65f);
    float x=p.x+p.size+28*s,w=ui.width-x-28*s;char text[120];
    ui.text(x,p.y,"EXPLORE THE COAST",1.7f*s,teal);
    ui.wrapped(x,p.y+28*s,"Find contracts, the Harbor Split trial, Harbor Motor Works and coastal launch sites.",1.45f*s,w,muted);
    ui.text(x,p.y+106*s,"GOLD   CONTRACT",1.3f*s,gold);ui.text(x,p.y+133*s,"TEAL   YOU / WAYPOINT",1.3f*s,teal);
    ui.text(x,p.y+160*s,"BLUE   BOAT",1.3f*s,{.42f,.72f,1});ui.text(x,p.y+187*s,"LILAC  AIRCRAFT",1.3f*s,{.88f,.83f,.97f});
    ui.text(x,p.y+214*s,"VIOLET HARBOR SPLIT",1.3f*s,violet);
    ui.text(x,p.y+241*s,"ORANGE MOTOR WORKS",1.3f*s,{.96f,.58f,.25f});
    ui.text(x,p.y+291*s,"WASD / LEFT STICK  PAN",1.2f*s,muted);
    ui.text(x,p.y+316*s,"WHEEL / +/- / LB RB  ZOOM",1.15f*s,muted);
    ui.text(x,p.y+341*s,"CLICK / ENTER / A  MARK",1.2f*s,muted);
    ui.text(x,p.y+366*s,"DELETE / X  CLEAR",1.2f*s,muted);
    ui.text(x,p.y+391*s,"F / Y  CENTER ON YOU",1.2f*s,muted);
    ui.text(x,p.y+416*s,"TAB / BACK / B  CLOSE",1.2f*s,muted);
    if(map.hasWaypoint){std::snprintf(text,sizeof(text),"WAYPOINT  %.2f KM",std::hypot(map.waypoint.x-game.player.x,map.waypoint.z-game.player.z)*.001f);ui.text(x,p.y+p.size-64*s,text,1.4f*s,teal);}
    std::snprintf(text,sizeof(text),"MAP WIDTH  %.2f KM",map.span*.001f);ui.text(p.x,p.y+p.size+18*s,text,1.3f*s,muted);
    ui.text(p.x+p.size-15*s,p.y+10*s,"N",1.5f*s,{.9f,.94f,.94f});
}
}
