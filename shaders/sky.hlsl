cbuffer Frame : register(b0) {
    row_major float4x4 viewProjection;
    float4 eyeTime;float4 sunDay;float4 weather;
    float4 cameraRight;float4 cameraUp;float4 cameraForward;float4 viewport;
};
#include "atmosphere.hlsli"
struct PixelInput {float4 position:SV_POSITION;float2 uv:TEXCOORD0;};
PixelInput VSMain(uint id:SV_VertexID) {
    PixelInput o;float2 uv=float2((id<<1)&2,id&2);o.uv=uv;
    o.position=float4(uv.x*2-1,1-uv.y*2,1,1);return o;
}
float4 PSMain(PixelInput i):SV_TARGET {
    return float4(skyRadianceAtPixel(i.position.xy),1);
}
