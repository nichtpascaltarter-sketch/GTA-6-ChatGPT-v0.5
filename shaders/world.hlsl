cbuffer Frame : register(b0) {
    row_major float4x4 viewProjection;
    float4 eyeTime;
    float4 sunDay;
    float4 weather;
    float4 cameraRight;
    float4 cameraUp;
    float4 cameraForward;
    float4 viewport;
};
struct VertexInput { float3 position:POSITION; float3 normal:NORMAL; float3 color:COLOR; float material:TEXCOORD0; };
struct PixelInput { float4 position:SV_POSITION; float3 world:TEXCOORD0; float3 normal:TEXCOORD1; float3 color:COLOR; nointerpolation float material:TEXCOORD2; };
PixelInput VSMain(VertexInput v) {
    PixelInput o;
    o.position=mul(float4(v.position,1),viewProjection);
    o.world=v.position; o.normal=v.normal; o.color=v.color; o.material=v.material;
    return o;
}
float hash(float2 p) { return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453); }
float noise(float2 p) {
    float2 i=floor(p),f=frac(p); f=f*f*(3-2*f);
    return lerp(lerp(hash(i),hash(i+float2(1,0)),f.x),lerp(hash(i+float2(0,1)),hash(i+1),f.x),f.y);
}
float3 skyColor(float3 direction) {
    float h=saturate(direction.y);
    float3 zenith=lerp(float3(.012,.023,.065),float3(.11,.34,.66),sunDay.w);
    float3 horizon=lerp(float3(.035,.05,.10),float3(.65,.78,.84),sunDay.w);
    float sunset=pow(saturate(1-abs(sunDay.y)*2.8),3)*sunDay.w;
    horizon+=float3(.40,.13,.015)*sunset*pow(saturate(dot(normalize(float3(direction.x,.07,direction.z)),normalize(float3(sunDay.x,.07,sunDay.z)))),4);
    float3 c=lerp(horizon,zenith,pow(h,.45));
    return lerp(c,float3(.16,.20,.23)*(.2+.8*sunDay.w),weather.x*.72);
}
float3 toneMap(float3 c) { c*=weather.y; return pow(saturate((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14)),1.0/2.2); }
#ifdef ENABLE_RAYTRACING
RaytracingAccelerationStructure scene : register(t0);
struct SceneVertex { float3 position; float3 normal; float3 color; float material; };
StructuredBuffer<SceneVertex> sceneVertices : register(t1);
StructuredBuffer<uint> sceneIndices : register(t2);
float sunVisibility(float3 p,float3 n) {
    if(sunDay.y<.02) return 1;
    RayDesc ray; ray.Origin=p+n*.055; ray.Direction=sunDay.xyz; ray.TMin=.04; ray.TMax=520;
    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH | RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(scene,RAY_FLAG_NONE,255,ray); while(q.Proceed()){}
    return q.CommittedStatus()==COMMITTED_TRIANGLE_HIT ? .08:1;
}
float3 reflectionColor(float3 p,float3 n,float3 direction) {
    RayDesc ray; ray.Origin=p+n*.08; ray.Direction=direction; ray.TMin=.04; ray.TMax=450;
    RayQuery<RAY_FLAG_FORCE_OPAQUE | RAY_FLAG_SKIP_PROCEDURAL_PRIMITIVES> q;
    q.TraceRayInline(scene,RAY_FLAG_NONE,255,ray); while(q.Proceed()){}
    if(q.CommittedStatus()!=COMMITTED_TRIANGLE_HIT) return skyColor(direction);
    uint base=q.CommittedPrimitiveIndex()*3;
    SceneVertex a=sceneVertices[sceneIndices[base]],b=sceneVertices[sceneIndices[base+1]],c=sceneVertices[sceneIndices[base+2]];
    float2 uv=q.CommittedTriangleBarycentrics(); float3 bary=float3(1-uv.x-uv.y,uv.x,uv.y);
    float3 albedo=a.color*bary.x+b.color*bary.y+c.color*bary.z;
    float3 normal=normalize(a.normal*bary.x+b.normal*bary.y+c.normal*bary.z);
    float3 lit=albedo*(float3(.14,.18,.23)*(.15+.85*sunDay.w)+max(0,dot(normal,sunDay.xyz))*sunDay.w*float3(1.6,1.4,1.1));
    if(a.material>1.5 && a.material<2.5) lit+=albedo*weather.w*1.1;
    float haze=1-exp(-q.CommittedRayT()*(.0015+weather.x*.002));
    return lerp(lit,skyColor(direction),haze);
}
#endif
float4 PSMain(PixelInput i):SV_TARGET {
    float3 n=normalize(i.normal),v=normalize(eyeTime.xyz-i.world),l=normalize(sunDay.xyz);
    float3 albedo=max(i.color,.008);
    float roughness=.76,metallic=0;
    float grain=noise(i.world.xz*2.1+i.world.y*.43);
    albedo*=.92+grain*.15;
    bool metal=i.material>.5 && i.material<1.5;
    bool glass=i.material>1.5 && i.material<2.5;
    bool water=i.material>2.5 && i.material<3.5;
    bool road=i.material>3.5;
    if(metal){metallic=.67;roughness=.23;}
    if(glass){metallic=.48;roughness=.18;}
    if(road){roughness=lerp(.86,.19,weather.x);albedo*=1-weather.x*.37;}
    if(water){
        float t=eyeTime.w;
        float dx=cos(i.world.x*.20+i.world.z*.07+t*.9)*.09+sin(i.world.z*.49+t*1.3)*.04;
        float dz=sin(i.world.z*.17+i.world.x*.11+t*.74)*.08+cos(i.world.x*.43-t)*.04;
        n=normalize(n+float3(dx,0,dz));roughness=.16;albedo*=.62;
    }
    float nl=saturate(dot(n,l)),nv=saturate(dot(n,v));
    float3 h=normalize(l+v);float nh=saturate(dot(n,h)),vh=saturate(dot(v,h));
    float a=roughness*roughness,a2=a*a;
    float d=a2/(3.14159265*pow(nh*nh*(a2-1)+1,2)+.00001);
    float k=(roughness+1)*(roughness+1)*.125;
    float g=(nl/(nl*(1-k)+k))*(nv/(nv*(1-k)+k));
    float3 f0=lerp(float3(.04,.04,.04),albedo,metallic);
    float3 f=f0+(1-f0)*pow(1-vh,5);
    float visibility=1;
#ifdef ENABLE_RAYTRACING
    visibility=sunVisibility(i.world,n);
#endif
    float3 sunColor=lerp(float3(2.6,1.02,.36),float3(2.25,2.08,1.78),saturate(sunDay.y*2));
    sunColor*=sunDay.w*(1-weather.x*.78);
    float3 diffuse=albedo*(1-metallic)/3.14159265;
    float3 color=(diffuse*(1-f)+d*g*f/max(.01,4*nl*nv))*sunColor*nl*visibility;
    float hemi=saturate(n.y*.5+.5);
    float3 ambient=lerp(float3(.085,.073,.065),float3(.30,.40,.53),hemi)*(.10+.90*sunDay.w);
    color+=ambient*albedo*(1-metallic*.55);
    color+=albedo*float3(.045,.060,.105)*(1-sunDay.w)*saturate(n.y+.25);
    if(glass) {
        float lit=step(.26,hash(floor(i.world.xz*.55)+floor(i.world.y*.29)));
        color+=albedo*float3(1.2,.86,.47)*weather.w*lit*1.45;
    }
    if(metal||glass||water||(road&&weather.x>.12)) {
        float3 r=reflect(-v,n), reflected=skyColor(r);
#ifdef ENABLE_RAYTRACING
        reflected=reflectionColor(i.world,n,r);
#endif
        float fresnel=.05+.95*pow(1-nv,5);
        float strength=water?lerp(.28,.92,fresnel):(glass?.50:(metal?.37:weather.x*.33));
        color=lerp(color,reflected,strength);
        if(water) color+=float3(.55,.65,.56)*pow(saturate(sin(i.world.x*.31+i.world.z*.18+eyeTime.w)*.5+.5),35)*.024;
    }
    float distance=length(i.world-eyeTime.xyz);
    float haze=1-exp(-distance*(.0011+weather.x*.0021));
    haze=max(haze,smoothstep(260,440,distance));
    color=lerp(color,skyColor(normalize(i.world-eyeTime.xyz)),saturate(haze));
    float2 rainUV=i.position.xy/viewport.xy;
    float rain=step(.991,hash(float2(floor((rainUV.x+rainUV.y*.10)*490),floor((rainUV.y+eyeTime.w*1.8)*38))));
    color+=rain*weather.x*.24;
    return float4(toneMap(color),1);
}
