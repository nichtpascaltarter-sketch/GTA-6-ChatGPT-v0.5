cbuffer Frame : register(b0) {
    row_major float4x4 viewProjection;
    float4 eyeTime;float4 sunDay;float4 weather;
    float4 cameraRight;float4 cameraUp;float4 cameraForward;float4 viewport;
};
Texture2D<float4> sceneColor : register(t5);
SamplerState sceneSampler : register(s1);
struct PixelInput {float4 position:SV_POSITION;float2 uv:TEXCOORD0;};
float3 highlights(float3 color) {
    float brightness=max(color.r,max(color.g,color.b));
    // A soft knee retains small highlights without blooming ordinary surfaces.
    float knee=clamp(brightness-.65,0,.50);
    float contribution=max(brightness-.90,knee*knee/.99);
    return color*max(contribution,0)/max(brightness,.0001);
}
float4 PSMain(PixelInput i):SV_TARGET {
    float3 color=max(sceneColor.SampleLevel(sceneSampler,i.uv,0).rgb,0);
    float2 spread=float2(2.0/viewport.x,2.0/viewport.y)*max(1.0,viewport.y/900.0);
    float3 bloom=highlights(color)*.25;
    bloom+=highlights(sceneColor.SampleLevel(sceneSampler,i.uv+float2(spread.x,0),0).rgb)*.125;
    bloom+=highlights(sceneColor.SampleLevel(sceneSampler,i.uv-float2(spread.x,0),0).rgb)*.125;
    bloom+=highlights(sceneColor.SampleLevel(sceneSampler,i.uv+float2(0,spread.y),0).rgb)*.125;
    bloom+=highlights(sceneColor.SampleLevel(sceneSampler,i.uv-float2(0,spread.y),0).rgb)*.125;
    bloom+=highlights(sceneColor.SampleLevel(sceneSampler,i.uv+spread,0).rgb)*.0625;
    bloom+=highlights(sceneColor.SampleLevel(sceneSampler,i.uv-spread,0).rgb)*.0625;
    bloom+=highlights(sceneColor.SampleLevel(sceneSampler,i.uv+spread*float2(-1,1),0).rgb)*.0625;
    bloom+=highlights(sceneColor.SampleLevel(sceneSampler,i.uv+spread*float2(1,-1),0).rgb)*.0625;
    color=(color+bloom*.12)*weather.y;
    float3 mapped=saturate((color*(2.51*color+.03))/(color*(2.43*color+.59)+.14));
    return float4(pow(mapped,1.0/2.2),1);
}
