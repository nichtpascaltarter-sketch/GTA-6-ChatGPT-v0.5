#include "../src/cinematics.h"
#include <cassert>
#include <cstring>
#include <limits>
#include <cstdio>

int main(){
    mc::Cinematic scene;assert(!scene.active());assert(!scene.dialogue());
    scene.start(-1,{},0);assert(!scene.active());scene.start(mc::Cinematic::chapterCount(),{},0);assert(!scene.active());
    mc::World world;world.stream({8,0,8});
    for(int chapter=0;chapter<mc::Cinematic::chapterCount();++chapter){
        scene.start(chapter,{8,0,8},.2f);assert(scene.active());
        const char* first=scene.dialogue()->speaker;
        scene.advance(std::numeric_limits<float>::quiet_NaN());assert(scene.elapsed()==0);
        scene.advance(-1);assert(scene.elapsed()==0);
        bool changed=false;unsigned frames=0;
        while(scene.active()){
            mc::Vec3 eye,target;scene.camera(world,eye,target);
            assert(std::isfinite(eye.x)&&std::isfinite(eye.y)&&std::isfinite(eye.z));
            assert(mc::length(eye-target)>.3f);assert(eye.y>=world.height(eye.x,eye.z)+.34f);
            assert(scene.dialogue()&&std::strlen(scene.dialogue()->text)>0);
            changed|=std::strcmp(first,scene.dialogue()->speaker)!=0;
            scene.advance(.05f);assert(++frames<500);
        }
        assert(changed);assert(frames>250);assert(!scene.dialogue());
        scene.start(chapter,{},0);scene.advance(0,true);assert(!scene.active());
    }
    mc::World closeWall;mc::Chunk chunk;chunk.solids.push_back({{0,-10,-10},{10,10,10}});closeWall.chunks.push_back(std::move(chunk));
    scene.start(0,{-.35f,0,0},mc::Pi*.5f+.72f);mc::Vec3 eye,target;scene.camera(closeWall,eye,target);
    assert(eye.x<=-.18f);assert(mc::length(eye-target)>.3f);
    std::puts("Cinematic timing, dialogue, camera and skip tests passed.");
}
