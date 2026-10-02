#ifndef MERIDIAN_ATMOSPHERE_HLSLI
#define MERIDIAN_ATMOSPHERE_HLSLI
// Authored sky model shared by the background and the world's visibility fade.
// Include after the Frame cbuffer; all radiance is linear HDR.
float atmosphereHash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
float atmosphereNoise(float2 p) {
    float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);
    return lerp(lerp(atmosphereHash(i),atmosphereHash(i+float2(1,0)),f.x),lerp(atmosphereHash(i+float2(0,1)),atmosphereHash(i+1),f.x),f.y);
}
float atmosphereFbm(float2 p) {
    return atmosphereNoise(p)*.53+atmosphereNoise(p*2.03)*.27+atmosphereNoise(p*4.07)*.13+atmosphereNoise(p*8.13)*.07;
}
float3 cameraRayAtPixel(float2 pixel) {
    // Both passes use SV_POSITION, including under MSAA. Reconstructing the
    // world ray from an interpolated point can disagree at geometry edges.
    float2 ndc=(pixel/viewport.xy)*float2(2,-2)+float2(-1,1);
    return normalize(cameraForward.xyz+cameraRight.xyz*(ndc.x*cameraRight.w)+cameraUp.xyz*(ndc.y*cameraUp.w));
}
float3 atmosphereBaseRadiance(float3 ray) {
    float h=saturate(ray.y);
    float3 zenith=lerp(float3(.012,.023,.065),float3(.07,.25,.52),sunDay.w);
    float3 horizon=lerp(float3(.035,.05,.10),float3(.48,.65,.74),sunDay.w);
    float sunset=pow(saturate(1-abs(sunDay.y)*2.8),3)*sunDay.w;
    horizon+=float3(.40,.13,.015)*sunset*pow(saturate(dot(normalize(float3(ray.x,.07,ray.z)),normalize(float3(sunDay.x,.07,sunDay.z)))),4);
    float3 c=lerp(horizon,zenith,pow(h,.45));
    c=lerp(c,float3(.16,.20,.23)*(.2+.8*sunDay.w),weather.x*.72);
    return c;
}
float3 atmosphereRadiance(float3 ray) {
    float3 c=atmosphereBaseRadiance(ray);
    float sun=dot(ray,sunDay.xyz);
    float sunset=pow(saturate(1-abs(sunDay.y)*2.8),3)*sunDay.w;
    float sunDisk=smoothstep(.99978,.99994,sun),sunGlow=pow(saturate(sun),128)*.35;
    c+=(sunDisk*7+sunGlow)*float3(1,.73,.39)*(1-weather.x*.9)*step(-.04,sunDay.y);
    float moon=dot(ray,normalize(float3(-sunDay.x,abs(sunDay.y)+.1,-sunDay.z)));
    c+=smoothstep(.99982,.99995,moon)*float3(.58,.65,.82)*(1-sunDay.w)*(1-weather.x);
    if(ray.y>.015){
        float2 p=ray.xz/(ray.y+.12)*2.6+float2(eyeTime.w*.008,eyeTime.w*.002);
        float density=smoothstep(.49-weather.x*.15,.75,atmosphereFbm(p));
        density*=smoothstep(.02,.22,ray.y);
        float cloudLight=atmosphereFbm(p+float2(.08,.09));
        float3 cloud=lerp(float3(.18,.23,.30),float3(.91,.91,.83),cloudLight)*(.15+.85*sunDay.w);
        cloud+=sunset*float3(.42,.13,.06)*pow(saturate(sun),4);
        c=lerp(c,cloud,density*(.82+weather.x*.18));
        float stars=step(.9991,atmosphereHash(floor(ray.xz/(ray.y+.2)*460)))*pow(saturate(ray.y),.3);
        c+=stars*(1-sunDay.w)*(1-density)*(1-weather.x)*.75;
    }
    return c;
}
float screenRainIntensity(float2 pixel) {
    float2 rainUV=pixel/viewport.xy;
    float rain=step(.991,atmosphereHash(float2(floor((rainUV.x+rainUV.y*.10)*490),floor((rainUV.y+eyeTime.w*1.8)*38))));
    return rain*weather.x*.24;
}
float3 skyRadianceAtPixel(float2 pixel) {
    return max(atmosphereRadiance(cameraRayAtPixel(pixel))+screenRainIntensity(pixel),0);
}
#endif
