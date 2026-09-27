// DXR 1.1 inline queries, native pixel grid. No upscaler or frame generation.
struct Vertex { float3 p; float3 n; float4 surface; float4 history; };
struct Instance { row_major float4x4 world; row_major float4x4 normal; row_major float4x4 previousWorld; float4 color; uint4 meta; };
RaytracingAccelerationStructure scene : register(t0);
StructuredBuffer<Vertex> vertices : register(t1);
StructuredBuffer<uint> indices : register(t2);
StructuredBuffer<Instance> instances : register(t3);
Texture2D<float4> images[26] : register(t4);
RWTexture2D<float4> targets[26] : register(u0);
cbuffer FilterPass : register(b1) { uint2 filterPass; };
cbuffer Constants : register(b0) {
 row_major float4x4 inverseVP;
 float4 eyeTime, sun, ambient;
 float4 lightPos[32], lightColor[32], lightDir[32], smoke[16];
 uint4 settings; // tier, bounces, sample index, local lights
 float4 frame; // width, height, fog density, smoke count
 uint4 sequence; // RNG, reset, reactive lighting, reserved
 row_major float4x4 previousVP, viewMatrix, previousView, currentVP;
};
static const float PI=3.141592654;
static uint rng;
float random(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return (rng+.5)/4294967296.0;}
float hash31(float3 p){p=frac(p*.1031);p+=dot(p,p.yzx+33.33);return frac((p.x+p.y)*p.z);}
float noise3(float3 p){float3 a=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(lerp(hash31(a),hash31(a+float3(1,0,0)),f.x),lerp(hash31(a+float3(0,1,0)),hash31(a+float3(1,1,0)),f.x),f.y),lerp(lerp(hash31(a+float3(0,0,1)),hash31(a+float3(1,0,1)),f.x),lerp(hash31(a+float3(0,1,1)),hash31(a+float3(1,1,1)),f.x),f.y),f.z);}
float3 hemisphere(float3 n){float a=2*PI*random(),r=sqrt(random());float3 t=normalize(cross(abs(n.y)<.99?float3(0,1,0):float3(1,0,0),n));return t*(r*cos(a))+cross(n,t)*(r*sin(a))+n*sqrt(max(0,1-r*r));}
float3 sky(float3 d){return ambient.rgb*lerp(.25,1.7,saturate(d.y*.5+.5));}
struct Hit {float3 p,n,albedo,emission;float rough,kind,distance; float3 previousPosition,currentPosition; uint objectId;};
bool trace(float3 origin,float3 direction,float limit,out Hit h){
 h=(Hit)0;RayDesc ray;ray.Origin=origin;ray.Direction=direction;ray.TMin=.002;ray.TMax=limit;
 RayQuery<RAY_FLAG_FORCE_OPAQUE> q;q.TraceRayInline(scene,0,255,ray);while(q.Proceed()){}
 if(q.CommittedStatus()!=COMMITTED_TRIANGLE_HIT)return false;
 Instance obj=instances[q.CommittedInstanceID()];uint tri=obj.meta.y+q.CommittedPrimitiveIndex()*3;
 Vertex a=vertices[obj.meta.x+indices[tri]],b=vertices[obj.meta.x+indices[tri+1]],c=vertices[obj.meta.x+indices[tri+2]];
 float2 uv=q.CommittedTriangleBarycentrics();float3 w=float3(1-uv.x-uv.y,uv);
 float4 surface=a.surface*w.x+b.surface*w.y+c.surface*w.z;
 h.objectId=q.CommittedInstanceID();float3 pa=sequence.w>0&&a.history.w>0?a.history.xyz:a.p,pb=sequence.w>0&&b.history.w>0?b.history.xyz:b.p,pc=sequence.w>0&&c.history.w>0?c.history.xyz:c.p;h.previousPosition=mul(float4(pa*w.x+pb*w.y+pc*w.z,1),obj.previousWorld).xyz;h.currentPosition=mul(float4(a.p*w.x+b.p*w.y+c.p*w.z,1),obj.world).xyz;
 h.distance=q.CommittedRayT();h.p=origin+direction*h.distance;
 h.n=normalize(mul(float4(a.n*w.x+b.n*w.y+c.n*w.z,0),obj.normal).xyz);if(dot(h.n,direction)>0)h.n=-h.n;
 h.kind=surface.w;h.rough=.78;float3 color=obj.color.rgb*surface.rgb;h.albedo=pow(max(color,0),2.2);h.emission=h.albedo*obj.color.w;
 if(h.kind>.5&&h.kind<6.8){float grain=noise3(h.p*5)*.58+noise3(h.p*18)*.28+noise3(h.p*65)*.14;h.albedo*=pow(.68+.55*grain+.18*noise3(h.p*1.3),2.2);}
 if(h.kind>6.8&&h.kind<7.5){h.albedo=float3(.006,.06,.12);h.rough=.055;h.n=normalize(h.n+float3(cos(h.p.x*4+eyeTime.w*1.2)*.22,0,cos(h.p.z*5-eyeTime.w*.9)*.18));}
 if(h.kind>7.5&&h.kind<8.5){float3 p=h.p*3+float3(eyeTime.w*.035,0,-eyeTime.w*.05);float cracks=1-smoothstep(.028,.085,abs(noise3(p)+.24*noise3(p*2.7)-.61));h.albedo=float3(.038,.013,.009);h.emission=lerp(float3(2.8,.15,.006),float3(4,1.25,.035),noise3(p*2))*cracks*(.85+.15*sin(eyeTime.w*1.2+h.p.x+h.p.z));}
 if(h.kind>8.5&&h.kind<9.5){float edge=abs(surface.x*2-1);h.albedo=0;h.emission=(lerp(float3(1,.025,.28),float3(.22,.055,1),noise3(h.p*2+eyeTime.w))*(2+5*(1-edge))+pow(saturate(1-edge),6)*float3(18,16,20))*(.72+.28*sin(eyeTime.w*37+floor(eyeTime.w*19)*3.1));}
 if(h.kind>9.5){float vein=1-smoothstep(.025,.07,abs(noise3(h.p*3.6)+.3*noise3(h.p*10.8)-.62));float pulse=.8+.2*sin(eyeTime.w*2+h.p.y*3);h.albedo=h.kind>10.5?float3(.11,.07,.18):float3(.018,.015,.022);h.rough=.92;h.emission=float3(2.6,.045,4.8)*vein*pulse;if(h.kind>10.5)h.emission+=float3(.25,.07,.55)*pulse;}
 if(obj.color.w>.5)h.albedo=0;
 return true;
}
float visibility(float3 p,float3 n,float3 l,float limit){
 RayDesc ray;ray.Origin=p+n*.003;ray.Direction=l;ray.TMin=.002;ray.TMax=max(.003,limit-.006);
 RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|RAY_FLAG_FORCE_OPAQUE> q;q.TraceRayInline(scene,0,255,ray);while(q.Proceed()){}return q.CommittedStatus()==COMMITTED_NOTHING;
}
float density(float3 p){float v=frame.z*exp(-max(p.y,0)*.35)*(.35+noise3(p*.22+float3(eyeTime.w*.035,0,0)));for(uint i=0;i<(uint)frame.w;i++)v+=.65*pow(saturate(1-length(p-smoke[i].xyz)/max(.1,smoke[i].w)),2);return v;}
float transmission(float3 p,float3 l,float distance){float sum=0;float stepSize=min(distance,24)/8;for(uint i=0;i<8;i++)sum+=density(p+l*((i+.5)*stepSize))*stepSize;return exp(-sum);}
float3 brdf(Hit h,float3 v,float3 l){float nl=saturate(dot(h.n,l)),nv=max(.001,dot(h.n,v));float3 halfway=normalize(v+l);float nh=saturate(dot(h.n,halfway)),vh=saturate(dot(v,halfway));float a=max(.025,h.rough*h.rough),a2=a*a;float D=a2/(PI*pow(nh*nh*(a2-1)+1,2));float k=pow(h.rough+1,2)/8;float G=nl/(nl*(1-k)+k)*nv/(nv*(1-k)+k);float3 F=.04+.96*pow(1-vh,5);return ((1-F)*h.albedo/PI+D*G*F/max(.001,4*nl*nv))*nl;}
float3 diffuseBrdf(Hit h,float3 v,float3 l){float vh=saturate(dot(v,normalize(v+l)));float3 F=.04+.96*pow(1-vh,5);return (1-F)*h.albedo/PI*saturate(dot(h.n,l));}
float3 oneLight(Hit h,float3 v,uint index,out float3 diffuse){
 diffuse=0;
 float3 l,radiance;float limit;
 if(index==0){l=normalize(-sun.xyz+hemisphere(normalize(-sun.xyz))*.00465);radiance=sun.www;limit=400;}
 else {uint i=index-1;float3 delta=lightPos[i].xyz-h.p;limit=length(delta);l=delta/max(.001,limit);float range=lightPos[i].w;float atten=pow(saturate(1-limit/range),2)/(1+limit*limit);float cone=lightDir[i].w<-1?1:smoothstep(lightDir[i].w,lightColor[i].w,dot(-l,normalize(lightDir[i].xyz)));radiance=lightColor[i].rgb*atten*cone;}
 if(dot(h.n,l)<=0||max(radiance.x,max(radiance.y,radiance.z))<.00001)return 0;
 float visible=visibility(h.p,h.n,l,limit);if(settings.x>=3)visible*=transmission(h.p,l,limit);
 diffuse=diffuseBrdf(h,v,l)*radiance*visible;return brdf(h,v,l)*radiance*visible;
}
float3 directSplit(Hit h,float3 v,bool allLights,out float3 diffuse){
 diffuse=0;
 uint count=settings.w+1;if(allLights){float3 sum=0;for(uint i=0;i<count;i++){float3 d;sum+=oneLight(h,v,i,d);diffuse+=d;}return sum;}
 // Importance-weighted reservoir: exactly one visibility ray for a sampled direct light.
 float total=0,chosenWeight=1;uint chosen=0;
 for(uint j=0;j<count;j++){float weight;if(j==0)weight=sun.w*max(.001,dot(h.n,-normalize(sun.xyz)));else{float3 delta=lightPos[j-1].xyz-h.p;float d=length(delta);weight=dot(lightColor[j-1].rgb,float3(.2126,.7152,.0722))*pow(saturate(1-d/lightPos[j-1].w),2)/(1+d*d)*max(.001,dot(h.n,delta/max(.001,d)));}weight=max(.00001,weight);total+=weight;if(random()*total<weight){chosen=j;chosenWeight=weight;}}
 float3 d;float3 value=oneLight(h,v,chosen,d)*total/chosenWeight;diffuse=d*total/chosenWeight;return value;
}
float3 direct(Hit h,float3 v,bool allLights){float3 d;return directSplit(h,v,allLights,d);}
float3 secondaryDistance(float3 p,float3 n,float3 dir,out float distance){Hit h;distance=0;if(!trace(p+n*.004,dir,400,h))return sky(dir);distance=h.distance;return h.emission+direct(h,-dir,settings.x>=3)+h.albedo*ambient.rgb*.16;}

float3 secondary(float3 p,float3 n,float3 dir){float distance;return secondaryDistance(p,n,dir,distance);}
[numthreads(8,8,1)] void Trace(uint3 id:SV_DispatchThreadID){
 if(any(id.xy>=(uint2)frame.xy))return;uint2 px=id.xy;rng=(px.x+px.y*(uint)frame.x)*747796405u+(sequence.x+1)*2891336453u+1;
 // Pixel centers remain aligned with guides and raster depth. No stochastic camera jitter.
 float2 ndc=(float2(px)+.5)/frame.xy*float2(2,-2)+float2(-1,1);float4 farPoint=mul(float4(ndc,1,1),inverseVP);float3 dir=normalize(farPoint.xyz/farPoint.w-eyeTime.xyz);
 Hit h;bool found=trace(eyeTime.xyz,dir,400,h);Hit primary=h;
 float specDistance=0;float3 diffuse=0,specular=0,emission=found?h.emission:sky(dir);
 float3 albedo=found?max(h.albedo,.001):1;float3 specAlbedo=found?.04+.96*pow(1-saturate(dot(-dir,h.n)),5):1;
 float depth=found?abs(mul(float4(h.p,1),viewMatrix).z):400;
 float4 previous=mul(float4(found?h.previousPosition:eyeTime.xyz+dir*400,1),previousVP);
 float4 current=mul(float4(found?h.currentPosition:eyeTime.xyz+dir*400,1),currentVP);
 float2 velocity=(previous.xy/max(previous.w,.0001)-current.xy/max(current.w,.0001))*float2(.5,-.5)*frame.xy;
 float oldDepth=found?abs(mul(float4(h.previousPosition,1),previousView).z):400;
 targets[1][px]=float4(found?h.n:float3(0,0,0),found?h.rough:1);
 targets[2][px]=float4(albedo,1);targets[3][px]=float4(specAlbedo,1);targets[4][px]=float4(depth,0,0,0);
 targets[5][px]=float4(velocity,oldDepth,previous.w>0?f16tof32(0x3c00+(found?h.objectId:8192)):0);targets[25][px]=float4(velocity,0,0);targets[6][px]=float4(emission,1);
 if(found){
  if(settings.x<4){float3 d;float3 total=directSplit(h,-dir,settings.x>=3,d);diffuse=d;specular=max(0,total-d);
   float ao=settings.x>=2?visibility(h.p,h.n,hemisphere(h.n),1.5):1;diffuse+=h.albedo*ambient.rgb*.22*ao;
   if(settings.x>=2)specular+=secondaryDistance(h.p,h.n,reflect(dir,h.n),specDistance)*specAlbedo*(1-h.rough*.7);
   if(settings.x>=3)diffuse+=h.albedo*secondary(h.p,h.n,hemisphere(h.n))*.65;
  }else{
   float3 throughputD=0,throughputS=0,throughput=1,incoming=dir;
   for(uint bounce=0;bounce<=settings.y;bounce++){
    float3 d;float3 lighting=directSplit(h,-incoming,false,d);
    if(bounce==0){diffuse+=d;specular+=max(0,lighting-d);}else{diffuse+=throughputD*(h.emission+lighting);specular+=throughputS*(h.emission+lighting);}
    if(bounce==settings.y)break;float3 v=-incoming;float prob=.35;float3 next;
    if(random()<prob){float a=max(.025,h.rough*h.rough),u=random(),phi=2*PI*random();float ct=sqrt((1-u)/(1+(a*a-1)*u)),st=sqrt(max(0,1-ct*ct));float3 tangent=normalize(cross(abs(h.n.y)<.99?float3(0,1,0):float3(1,0,0),h.n));float3 halfV=normalize(tangent*(st*cos(phi))+cross(h.n,tangent)*(st*sin(phi))+h.n*ct);next=reflect(incoming,halfV);}else next=hemisphere(h.n);
    float nl=dot(h.n,next);if(nl<=0)break;float3 halfV=normalize(v+next);float nh=saturate(dot(h.n,halfV)),vh=max(.0001,dot(v,halfV));float a=max(.025,h.rough*h.rough),D=a*a/(PI*pow(nh*nh*(a*a-1)+1,2));float pdf=max(.00001,prob*D*nh/(4*vh)+(1-prob)*nl/PI);
    float3 weight=brdf(h,v,next)/pdf;
    if(bounce==0){throughputD=diffuseBrdf(h,v,next)/pdf;throughputS=max(0,weight-throughputD);}else{throughputD*=weight;throughputS*=weight;}
    throughput=throughputD+throughputS;
    if(bounce>=1){float survive=clamp(max(throughput.x,max(throughput.y,throughput.z)),.05,.95);if(random()>survive)break;throughputD/=survive;throughputS/=survive;}
    float3 origin=h.p+h.n*.004;Hit nextHit;if(!trace(origin,next,400,nextHit)){diffuse+=throughputD*sky(next);specular+=throughputS*sky(next);break;}
    if(bounce==0)specDistance=nextHit.distance;float trans=transmission(origin,next,nextHit.distance);throughputD*=trans;throughputS*=trans;h=nextHit;incoming=next;
   }
  }
 }
 if(!all(isfinite(diffuse)))diffuse=0;if(!all(isfinite(specular)))specular=0;
 diffuse=max(diffuse,0);specular=max(specular,0);
 targets[7][px]=float4(min(diffuse/albedo,60000),1);targets[8][px]=float4(min(specular/specAlbedo,60000),1);
 targets[24][px]=float4(specDistance,0,0,0);targets[0][px]=float4(min(diffuse+specular+emission,60000),1);
}
#include "svgf.hlsl"
