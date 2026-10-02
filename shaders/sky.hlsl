cbuffer Frame : register(b0) {
    row_major float4x4 viewProjection;
    float4 eyeTime;float4 sunDay;float4 weather;
    float4 cameraRight;float4 cameraUp;float4 cameraForward;float4 viewport;
};
struct PixelInput {float4 position:SV_POSITION;float2 uv:TEXCOORD0;};
PixelInput VSMain(uint id:SV_VertexID) {
    PixelInput o;float2 uv=float2((id<<1)&2,id&2);o.uv=uv;
    o.position=float4(uv.x*2-1,1-uv.y*2,1,1);return o;
}
float hash(float2 p){return frac(sin(dot(p,float2(127.1,311.7)))*43758.5453);}
float noise(float2 p){float2 i=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(hash(i),hash(i+float2(1,0)),f.x),lerp(hash(i+float2(0,1)),hash(i+1),f.x),f.y);}
float fbm(float2 p){return noise(p)*.53+noise(p*2.03)*.27+noise(p*4.07)*.13+noise(p*8.13)*.07;}
float3 toneMap(float3 c){c*=weather.y;return pow(saturate((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14)),1.0/2.2);}
float4 PSMain(PixelInput i):SV_TARGET {
    float2 ndc=i.uv*float2(2,-2)+float2(-1,1);
    float3 ray=normalize(cameraForward.xyz+cameraRight.xyz*(ndc.x*cameraRight.w)+cameraUp.xyz*(ndc.y*cameraUp.w));
    float h=saturate(ray.y),sun=dot(ray,sunDay.xyz);
    float3 zenith=lerp(float3(.012,.023,.065),float3(.11,.34,.66),sunDay.w);
    float3 horizon=lerp(float3(.035,.05,.10),float3(.65,.78,.84),sunDay.w);
    float sunset=pow(saturate(1-abs(sunDay.y)*2.8),3)*sunDay.w;
    horizon+=float3(.40,.13,.015)*sunset*pow(saturate(dot(normalize(float3(ray.x,.07,ray.z)),normalize(float3(sunDay.x,.07,sunDay.z)))),4);
    float3 c=lerp(horizon,zenith,pow(h,.45));
    c=lerp(c,float3(.16,.20,.23)*(.2+.8*sunDay.w),weather.x*.72);
    float sunDisk=smoothstep(.99978,.99994,sun),sunGlow=pow(saturate(sun),128)*.35;
    c+=(sunDisk*7+sunGlow)*float3(1,.73,.39)*(1-weather.x*.9)*step(-.04,sunDay.y);
    float moon=dot(ray,normalize(float3(-sunDay.x,abs(sunDay.y)+.1,-sunDay.z)));
    c+=smoothstep(.99982,.99995,moon)*float3(.58,.65,.82)*(1-sunDay.w)*(1-weather.x);
    if(ray.y>.015){
        float2 p=ray.xz/(ray.y+.12)*2.6+float2(eyeTime.w*.008,eyeTime.w*.002);
        float density=smoothstep(.49-weather.x*.15,.75,fbm(p));
        density*=smoothstep(.02,.22,ray.y);
        float cloudLight=fbm(p+float2(.08,.09));
        float3 cloud=lerp(float3(.18,.23,.30),float3(.91,.91,.83),cloudLight)*(.15+.85*sunDay.w);
        cloud+=sunset*float3(.42,.13,.06)*pow(saturate(sun),4);
        c=lerp(c,cloud,density*(.82+weather.x*.18));
        float stars=step(.9991,hash(floor(ray.xz/(ray.y+.2)*460)))*pow(saturate(ray.y),.3);
        c+=stars*(1-sunDay.w)*(1-density)*(1-weather.x)*.75;
    }
    float2 rainUV=i.position.xy/viewport.xy;
    float rain=step(.991,hash(float2(floor((rainUV.x+rainUV.y*.10)*490),floor((rainUV.y+eyeTime.w*1.8)*38))));
    c+=rain*weather.x*.24;
    return float4(toneMap(c),1);
}
