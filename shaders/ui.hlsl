cbuffer Frame : register(b0) {
    row_major float4x4 viewProjection;
    float4 eyeTime;float4 sunDay;float4 weather;
    float4 cameraRight;float4 cameraUp;float4 cameraForward;float4 viewport;
};
struct VertexInput {float2 position:POSITION;float4 color:COLOR;};
struct PixelInput {float4 position:SV_POSITION;float4 color:COLOR;};
PixelInput VSMain(VertexInput v){PixelInput o;o.position=float4(v.position.x/viewport.x*2-1,1-v.position.y/viewport.y*2,0,1);o.color=v.color;return o;}
float4 PSMain(PixelInput i):SV_TARGET{return i.color;}
