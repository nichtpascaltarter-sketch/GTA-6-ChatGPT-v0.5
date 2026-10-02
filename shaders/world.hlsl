cbuffer Frame : register(b0) {
    row_major float4x4 viewProjection;
    float4 eyeTime;
    float4 sunDay;
    float4 weather;
    float4 cameraRight;
    float4 cameraUp;
    float4 cameraForward;
    float4 viewport;
    row_major float4x4 lightProjection;
    float4 visibility; // fog start/end, horizontal metric, camera far plane
};
#include "atmosphere.hlsli"
struct VertexInput { float3 position:POSITION; float3 normal:NORMAL; float3 color:COLOR; float material:TEXCOORD0; };
struct PixelInput { float4 position:SV_POSITION; float3 world:TEXCOORD0; float3 normal:TEXCOORD1; float3 color:COLOR; nointerpolation float material:TEXCOORD2; float4 shadow:TEXCOORD3; };
PixelInput VSMain(VertexInput v) {
    PixelInput o;
    o.position=mul(float4(v.position,1),viewProjection);
    o.world=v.position; o.normal=v.normal; o.color=v.color; o.material=v.material;
    o.shadow=mul(float4(v.position+v.normal*.025,1),lightProjection);
    return o;
}
float4 VSShadow(VertexInput v):SV_POSITION { return mul(float4(v.position,1),lightProjection); }
Texture2D<float> shadowMap : register(t3);
SamplerComparisonState shadowSampler : register(s0);
float rasterVisibility(float3 projected,float3 px,float3 py,float normalDotLight) {
    // Follow the receiver plane across the PCF footprint. A constant depth
    // comparison causes striping on walls nearly parallel to the sun rays.
    float2 uvx=px.xy*float2(.5,-.5),uvy=py.xy*float2(.5,-.5);
    float determinant=uvx.x*uvy.y-uvx.y*uvy.x;
    float2 depthSlope=0;
    if(abs(determinant)>1e-10) depthSlope=float2(px.z*uvy.y-py.z*uvx.y,py.z*uvx.x-px.z*uvy.x)/determinant;
    if(projected.z<=0 || projected.z>=1) return 1;
    float edge=max(abs(projected.x),abs(projected.y));
    if(edge>=1) return 1;
    float2 uv=projected.xy*float2(.5,-.5)+.5;
    float bias=.000015+.000025*(1-normalDotLight)+min(.002,dot(abs(depthSlope),float2(viewport.z,viewport.z))*.5);
    float visibility=0;
    [unroll] for(int y=-1;y<=1;++y) {
        [unroll] for(int x=-1;x<=1;++x) {
            float2 offset=float2(x,y)*viewport.z;
            visibility+=shadowMap.SampleCmpLevelZero(shadowSampler,uv+offset,projected.z+dot(depthSlope,offset)-bias);
        }
    }
    visibility/=9;
    float coverage=(1-smoothstep(.82,.98,edge))*smoothstep(.02,.12,sunDay.y);
    return lerp(1,visibility,coverage);
}
float surfaceGrain(float3 position,float3 normal) {
    // Project onto the surface's dominant plane using equal metres per axis.
    // Mixing height into XZ stretched and sheared the grain into facade bands.
    float3 axis=abs(normal);
    float2 uv=axis.y>=axis.x&&axis.y>=axis.z?position.xz:(axis.x>=axis.z?position.zy:position.xy);
    return atmosphereNoise(uv*2.1);
}
// Reflections retain the cheaper gradient/weather approximation; primary
// visibility fading uses the full sky, including clouds and celestial discs.
float3 reflectionSkyApproximation(float3 direction) { return atmosphereBaseRadiance(direction); }
float3 surfaceEmission(float3 color) {
    // Window occupancy is authored per pane by its warm tint. Cool unoccupied
    // panes remain reflective; bright signs, lenses and signal lamps stay lit.
    float lamp=step(.70,max(color.r,max(color.g,color.b)));
    float occupied=step(.28,color.r)*step(color.b*1.15,color.r);
    return color*lerp(float3(1.2,.86,.47),float3(1,1,1),lamp)*max(lamp,occupied)*weather.w*1.3;
}
struct LocalLight {float3 position;float radius;float3 color;float intensity;float3 direction;float cone;};
StructuredBuffer<LocalLight> localLights : register(t4);
float3 localLighting(float3 position,float3 n,float3 v,float3 albedo,float roughness,float metallic) {
    float3 result=0;
    float nv=saturate(dot(n,v));
    float a=roughness*roughness,a2=a*a,k=(roughness+1)*(roughness+1)*.125;
    float3 f0=lerp(float3(.04,.04,.04),albedo,metallic);
    [loop] for(uint index=0;index<(uint)viewport.w;++index) {
        LocalLight light=localLights[index];
        float3 delta=light.position-position;
        float distance2=dot(delta,delta),radius2=light.radius*light.radius;
        if(distance2>=radius2||light.intensity<=0) continue;
        float3 direction=delta*rsqrt(max(distance2,.001));
        float nl=saturate(dot(n,direction));
        if(nl<=0) continue;
        float cone=1;
        if(light.cone>-.999) cone=smoothstep(light.cone,min(.999,light.cone+.12),dot(light.direction,-direction));
        float fade=saturate(1-distance2/max(radius2,.001));
        float attenuation=cone*fade*fade*light.intensity/(1+distance2);
        float3 h=normalize(v+direction);
        float nh=saturate(dot(n,h)),vh=saturate(dot(v,h));
        float d=a2/(3.14159265*pow(nh*nh*(a2-1)+1,2)+.00001);
        float g=(nl/(nl*(1-k)+k))*(nv/(nv*(1-k)+k));
        float3 f=f0+(1-f0)*pow(1-vh,5);
        float3 diffuse=(1-f)*albedo*(1-metallic)/3.14159265;
        float3 specular=d*g*f/max(.01,4*nl*nv);
        result+=(diffuse+specular)*light.color*attenuation*nl;
    }
    return result;
}
#ifdef ENABLE_RAYTRACING
RaytracingAccelerationStructure scene : register(t0);
struct SceneVertex { float3 position; float3 normal; float3 color; float material; };
StructuredBuffer<SceneVertex> sceneVertices : register(t1);
StructuredBuffer<uint> sceneIndices : register(t2);
struct InstanceMetadata { uint firstVertex; uint firstIndex; };
StructuredBuffer<InstanceMetadata> sceneInstances : register(t6);
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
    if(q.CommittedStatus()!=COMMITTED_TRIANGLE_HIT) return reflectionSkyApproximation(direction);
    InstanceMetadata instance=sceneInstances[q.CommittedInstanceID()];
    uint base=instance.firstIndex+q.CommittedPrimitiveIndex()*3;
    SceneVertex a=sceneVertices[instance.firstVertex+sceneIndices[base]],b=sceneVertices[instance.firstVertex+sceneIndices[base+1]],c=sceneVertices[instance.firstVertex+sceneIndices[base+2]];
    float2 uv=q.CommittedTriangleBarycentrics(); float3 bary=float3(1-uv.x-uv.y,uv.x,uv.y);
    float3 albedo=a.color*bary.x+b.color*bary.y+c.color*bary.z;
    float3 normal=normalize(a.normal*bary.x+b.normal*bary.y+c.normal*bary.z);
    float3 lit=albedo*(float3(.14,.18,.23)*(.15+.85*sunDay.w)+max(0,dot(normal,sunDay.xyz))*sunDay.w*float3(1.6,1.4,1.1));
    if(a.material>1.5 && a.material<2.5) lit+=surfaceEmission(albedo);
    float haze=1-exp(-q.CommittedRayT()*(.0015+weather.x*.002));
    return lerp(lit,reflectionSkyApproximation(direction),haze);
}
#endif
float4 PSMain(PixelInput i):SV_TARGET {
    // Evaluate derivatives before the distance branch: neighboring pixels may
    // take different fog paths, but PCF's receiver-plane gradients stay valid.
#ifndef FAR_GEOMETRY
    float3 projectedShadow=i.shadow.xyz/i.shadow.w;
    float3 shadowDx=ddx(projectedShadow),shadowDy=ddy(projectedShadow);
#endif
    float3 eyeOffset=i.world-eyeTime.xyz;
    float distance=length(eyeOffset);
    float coverageDistance=visibility.z>.5?length(eyeOffset.xz):distance;
    [branch] if(coverageDistance>=visibility.y) return float4(skyRadianceAtPixel(i.position.xy),1);
    float3 n=normalize(i.normal),v=normalize(eyeTime.xyz-i.world),l=normalize(sunDay.xyz);
    float3 albedo=max(i.color,.008);
    float roughness=.76,metallic=0;
#ifndef FAR_GEOMETRY
    float grain=surfaceGrain(i.world,n);
    albedo*=.97+grain*.06;
#endif
    bool metal=i.material>.5 && i.material<1.5;
    bool glass=i.material>1.5 && i.material<2.5;
    bool water=i.material>2.5 && i.material<3.5;
    bool road=i.material>3.5;
    if(metal){metallic=.67;roughness=.23;}
    if(glass){metallic=.48;roughness=.18;}
    if(road){
        roughness=lerp(.86,.19,weather.x);
#ifndef FAR_GEOMETRY
        float aggregate=atmosphereNoise(i.world.xz*24);
        albedo*=.92+aggregate*.13;
#endif
        albedo*=1-weather.x*.37;
    }
    if(!metal&&!glass&&!water&&!road) albedo*=1-.08*exp(-max(i.world.y,0)*1.4);
    if(water){
#ifndef FAR_GEOMETRY
        float t=eyeTime.w;
        float dx=cos(i.world.x*.20+i.world.z*.07+t*.9)*.09+sin(i.world.z*.49+t*1.3)*.04;
        float dz=sin(i.world.z*.17+i.world.x*.11+t*.74)*.08+cos(i.world.x*.43-t)*.04;
        n=normalize(n+float3(dx,0,dz));
#endif
        roughness=.16;albedo*=.62;
    }
    float nl=saturate(dot(n,l)),nv=saturate(dot(n,v));
    float3 h=normalize(l+v);float nh=saturate(dot(n,h)),vh=saturate(dot(v,h));
    float a=roughness*roughness,a2=a*a;
    float d=a2/(3.14159265*pow(nh*nh*(a2-1)+1,2)+.00001);
    float k=(roughness+1)*(roughness+1)*.125;
    float g=(nl/(nl*(1-k)+k))*(nv/(nv*(1-k)+k));
    float3 f0=lerp(float3(.04,.04,.04),albedo,metallic);
    float3 f=f0+(1-f0)*pow(1-vh,5);
    float shadowVisibility=1;
#ifndef FAR_GEOMETRY
    shadowVisibility=rasterVisibility(projectedShadow,shadowDx,shadowDy,nl);
#ifdef ENABLE_RAYTRACING
    shadowVisibility=min(shadowVisibility,sunVisibility(i.world,n));
#endif
#endif
    float3 sunColor=lerp(float3(3.1,1.20,.42),float3(2.6,2.42,2.12),saturate(sunDay.y*2));
    sunColor*=sunDay.w*(1-weather.x*.78);
    float3 diffuse=albedo*(1-metallic)/3.14159265;
    float3 color=(diffuse*(1-f)+d*g*f/max(.01,4*nl*nv))*sunColor*nl*shadowVisibility;
    float hemi=saturate(n.y*.5+.5);
    float3 ambient=lerp(float3(.060,.054,.048),float3(.20,.28,.37),hemi)*(.15+.85*sunDay.w);
    color+=ambient*albedo*(1-metallic*.55);
    color+=albedo*float3(.045,.060,.105)*(1-sunDay.w)*saturate(n.y+.25);
    float3 moonDirection=normalize(float3(-sunDay.x,abs(sunDay.y)+.15,-sunDay.z));
    color+=albedo*float3(.085,.11,.17)*saturate(dot(n,moonDirection))*(1-sunDay.w);
    if(glass) color+=surfaceEmission(albedo);
    if(metal||glass||water||(road&&weather.x>.12)) {
        float3 r=reflect(-v,n), reflected=reflectionSkyApproximation(r);
#ifdef ENABLE_RAYTRACING
        reflected=reflectionColor(i.world,n,r);
#endif
        float fresnel=.05+.95*pow(1-nv,5);
        float strength=water?lerp(.28,.92,fresnel):(glass?(.12+.46*fresnel):(metal?.37:weather.x*.33));
        color=lerp(color,reflected,strength);
#ifndef FAR_GEOMETRY
        if(water) color+=float3(.55,.65,.56)*pow(saturate(sin(i.world.x*.31+i.world.z*.18+eyeTime.w)*.5+.5),35)*.024;
#endif
    }
#ifndef FAR_GEOMETRY
    color+=localLighting(i.world,n,v,albedo,roughness,metallic);
#endif
    float haze=1-exp(-distance*(.00065+weather.x*.0021));
    float outerFade=smoothstep(visibility.x,visibility.y,coverageDistance);
    haze=max(haze,outerFade);
    float3 fogRay=cameraRayAtPixel(i.position.xy);
    float3 fogColor=atmosphereBaseRadiance(fogRay);
    // Physical haze uses slant distance; streamed coverage uses horizontal
    // distance so high aircraft still see the terrain below. Only the outer
    // fade needs the full cloud/disc/star model used by the background.
    [branch] if(outerFade>0) fogColor=lerp(fogColor,atmosphereRadiance(fogRay),outerFade);
    color=lerp(color,fogColor,saturate(haze));
    color+=screenRainIntensity(i.position.xy);
    return float4(max(color,0),1);
}
