#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace mc {
constexpr float Pi = 3.14159265358979323846f;
struct Vec2 { float x=0, y=0; };
struct Vec3 {
    float x=0,y=0,z=0;
    Vec3 operator+(Vec3 b) const { return {x+b.x,y+b.y,z+b.z}; }
    Vec3 operator-(Vec3 b) const { return {x-b.x,y-b.y,z-b.z}; }
    Vec3 operator-() const { return {-x,-y,-z}; }
    Vec3 operator*(float s) const { return {x*s,y*s,z*s}; }
    Vec3 operator/(float s) const { return *this*(1.0f/s); }
    Vec3& operator+=(Vec3 b) { *this=*this+b; return *this; }
    Vec3& operator-=(Vec3 b) { *this=*this-b; return *this; }
};
inline float dot(Vec3 a,Vec3 b) {return a.x*b.x+a.y*b.y+a.z*b.z;}
inline Vec3 cross(Vec3 a,Vec3 b) {return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
inline float length(Vec3 a) {return std::sqrt(dot(a,a));}
inline Vec3 normalized(Vec3 a) {float l=length(a); return l>1e-6f?a/l:Vec3{};}
inline float clamp(float x,float lo,float hi) {return std::max(lo,std::min(x,hi));}
inline float lerp(float a,float b,float t) {return a+(b-a)*t;}
inline Vec3 lerp(Vec3 a,Vec3 b,float t) {return a+(b-a)*t;}
inline float wrapAngle(float a) {return std::remainder(a,2*Pi);}
inline Vec3 forward(float yaw) {return {std::sin(yaw),0,std::cos(yaw)};}
inline Vec3 right(float yaw) {return {std::cos(yaw),0,-std::sin(yaw)};}
struct Mat4 { float m[16]{}; };
// Row vectors, row-major matrices, left-handed world, Y up, metres.
inline Mat4 multiply(const Mat4& a,const Mat4& b) {
    Mat4 r; for(int i=0;i<4;++i) for(int j=0;j<4;++j) for(int k=0;k<4;++k) r.m[i*4+j]+=a.m[i*4+k]*b.m[k*4+j]; return r;
}
inline Mat4 lookAt(Vec3 eye,Vec3 target) {
    Vec3 z=normalized(target-eye),x=normalized(cross({0,1,0},z)),y=cross(z,x);
    return {{x.x,y.x,z.x,0,x.y,y.y,z.y,0,x.z,y.z,z.z,0,-dot(x,eye),-dot(y,eye),-dot(z,eye),1}};
}
inline Mat4 perspective(float fov,float aspect,float zn,float zf) {
    float y=1/std::tan(fov*.5f),x=y/aspect,q=zf/(zf-zn);
    return {{x,0,0,0,0,y,0,0,0,0,q,1,0,0,-zn*q,0}};
}
inline uint32_t hash32(uint32_t x) {x^=x>>16; x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return x;}
inline float random01(uint32_t x) {return float(hash32(x)&0xffffffu)/16777216.0f;}
}
