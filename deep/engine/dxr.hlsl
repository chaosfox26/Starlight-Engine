#define SL_SURFACE_ALBEDO t30
#define SL_SURFACE_NORMAL t31
#define SL_SURFACE_ORM t32
#include "surface_material.hlsl"
#include "surface_lighting.hlsl"
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
 float4 weather,weatherLow[64],weatherHigh[64];
};
static const float PI=3.141592654;
static uint rng;
float random(){rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return (rng+.5)/4294967296.0;}
float hash31(float3 p){p=frac(p*.1031);p+=dot(p,p.yzx+33.33);return frac((p.x+p.y)*p.z);}
float noise3(float3 p){float3 a=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(lerp(hash31(a),hash31(a+float3(1,0,0)),f.x),lerp(hash31(a+float3(0,1,0)),hash31(a+float3(1,1,0)),f.x),f.y),lerp(lerp(hash31(a+float3(0,0,1)),hash31(a+float3(1,0,1)),f.x),lerp(hash31(a+float3(0,1,1)),hash31(a+float3(1,1,1)),f.x),f.y),f.z);}
float3 hemisphere(float3 n){float a=2*PI*random(),r=sqrt(random());float3 t=normalize(cross(abs(n.y)<.99?float3(0,1,0):float3(1,0,0),n));return t*(r*cos(a))+cross(n,t)*(r*sin(a))+n*sqrt(max(0,1-r*r));}
float3 sky(float3 d){return slSkyRadiance(d,ambient.rgb,sun,1/max(frame.y,1),weather.y);}
struct Hit {float3 p,n,albedo,emission;float rough,kind,distance,ao,waterFoam,waterThickness; float3 previousPosition,currentPosition; uint objectId; bool grassBlade;};
bool trace(float3 origin,float3 direction,float limit,out Hit h,bool skipWater=false){
 h=(Hit)0;RayDesc ray;ray.Origin=origin;ray.Direction=direction;ray.TMin=.002;ray.TMax=limit;
 RayQuery<RAY_FLAG_NONE> q;q.TraceRayInline(scene,skipWater?RAY_FLAG_FORCE_NON_OPAQUE:RAY_FLAG_FORCE_OPAQUE,255,ray);
 while(q.Proceed())if(q.CandidateType()==CANDIDATE_NON_OPAQUE_TRIANGLE){
  Instance object=instances[q.CandidateInstanceID()];uint candidateTriangle=object.meta.y+q.CandidatePrimitiveIndex()*3;
  float candidateKind=vertices[object.meta.x+indices[candidateTriangle]].surface.w;
  if(!skipWater||candidateKind<6.8||candidateKind>7.5)q.CommitNonOpaqueTriangleHit();
 }
 if(q.CommittedStatus()!=COMMITTED_TRIANGLE_HIT)return false;
 Instance obj=instances[q.CommittedInstanceID()];uint tri=obj.meta.y+q.CommittedPrimitiveIndex()*3;
 Vertex a=vertices[obj.meta.x+indices[tri]],b=vertices[obj.meta.x+indices[tri+1]],c=vertices[obj.meta.x+indices[tri+2]];
 float2 uv=q.CommittedTriangleBarycentrics();float3 w=float3(1-uv.x-uv.y,uv);
 float4 surface=a.surface*w.x+b.surface*w.y+c.surface*w.z;
 h.objectId=q.CommittedInstanceID();float3 pa=sequence.w>0&&a.history.w>0?a.history.xyz:a.p,pb=sequence.w>0&&b.history.w>0?b.history.xyz:b.p,pc=sequence.w>0&&c.history.w>0?c.history.xyz:c.p;h.previousPosition=mul(float4(pa*w.x+pb*w.y+pc*w.z,1),obj.previousWorld).xyz;h.currentPosition=mul(float4(a.p*w.x+b.p*w.y+c.p*w.z,1),obj.world).xyz;
 h.distance=q.CommittedRayT();h.p=origin+direction*h.distance;
 h.n=normalize(mul(float4(a.n*w.x+b.n*w.y+c.n*w.z,0),obj.normal).xyz);if(dot(h.n,direction)>0)h.n=-h.n;
 float3 geometryNormal=h.n;
 h.kind=slSurfaceKind(surface.w);h.grassBlade=slGrassBlade(surface.w);h.rough=.78;h.ao=1;float3 color=obj.color.rgb*surface.rgb;h.albedo=pow(max(color,0),2.2);h.emission=h.albedo*obj.color.w;
 if(h.grassBlade){SlSurfaceMaterial material=slGrassBladeMaterial(surface.rgb,h.n,max(.0001,h.distance/max(frame.y,1)*.9));h.albedo=pow(max(obj.color.rgb,0),2.2)*material.tint;h.n=material.normal;h.rough=material.roughness;h.ao=material.ao;}
 else if(h.kind>.5&&h.kind<6.8){float footprint=max(.0001,h.distance/max(frame.y,1)*.9);SlSurfaceMaterial material=slSurfaceMaterial(h.p,h.n,h.kind,footprint);h.albedo*=material.tint;h.n=material.normal;h.rough=material.roughness;h.ao=material.ao;}
 if(h.kind>6.8&&h.kind<7.5){SlWaterMaterial water;if(slWaterDroplet(h.kind)){water=slWaterDropMaterial(h.n);h.waterThickness=max(.001,surface.y);}else water=slWaterMaterial(h.p,h.n,eyeTime.w,surface.x*2,max(.0001,h.distance/max(frame.y,1)*.9/max(.12,abs(dot(h.n,direction)))));h.albedo=water.color;h.rough=water.roughness;h.n=water.normal;h.waterFoam=water.foam;}
 if(h.kind>7.5&&h.kind<8.5){SlLavaMaterial lava=slLavaMaterial(h.p,h.n,eyeTime.w,max(.0001,h.distance/max(frame.y,1)*.9),h.kind,surface.rgb);h.albedo=lava.color;h.emission=lava.emission;h.rough=lava.roughness;h.n=lava.normal;}
 if(h.kind>8.5&&h.kind<9.5){if(h.kind<9.3)surface.y=noise3(h.p*2+eyeTime.w);h.albedo=0;h.emission=slLightningEmission(h.kind,surface.rgb,h.n,-direction,eyeTime.w);}
 if(h.kind>9.5){SlMagicStoneMaterial magic=slMagicStoneMaterial(h.p,h.n,h.kind,h.albedo,eyeTime.w,max(.0001,h.distance/max(frame.y,1)*.9));h.albedo=magic.color;h.n=magic.normal;h.rough=magic.roughness;h.ao=magic.ao;h.emission=magic.emission;}
 if(h.kind<6.8){float wet=slWeatherExposure(h.p,geometryNormal,weather,weatherLow,weatherHigh);slWetSurface(h.p,geometryNormal,h.kind,wet,eyeTime.w,max(.0001,h.distance/max(frame.y,1)*.9),h.albedo,h.n,h.rough);}
 if(obj.color.w>.5&&!(h.kind>7.5&&h.kind<8.5))h.albedo=0;
 return true;
}
float3 visibility(float3 p,float3 n,float3 l,float limit,bool lightEndpoint=false){
 RayDesc ray;ray.Origin=p+n*.003;ray.Direction=l;ray.TMin=.002;ray.TMax=max(.003,limit-.006);
 // A luminous channel enclosing its own point-light sample must not shadow
 // that sample. Other emitters and all non-emissive surfaces still occlude.
 RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|RAY_FLAG_FORCE_NON_OPAQUE> q;q.TraceRayInline(scene,0,255,ray);
 float waterDistance=0,surfaceDistance=0,nearestSurface=ray.TMax+1;bool startsUnderwater=false;
 while(q.Proceed())if(q.CandidateType()==CANDIDATE_NON_OPAQUE_TRIANGLE){
  Instance object=instances[q.CandidateInstanceID()];uint tri=object.meta.y+q.CandidatePrimitiveIndex()*3;
  Vertex a=vertices[object.meta.x+indices[tri]];float kind=a.surface.w;
  if(kind>6.8&&kind<7.5){
   if(slWaterDroplet(kind)){
    // Closed droplets have two boundaries. Their stored thickness gives a
    // bounded thin-volume approximation, independent of traversal order.
    Vertex b=vertices[object.meta.x+indices[tri+1]],c=vertices[object.meta.x+indices[tri+2]];
    float2 uv=q.CandidateTriangleBarycentrics();
    waterDistance+=max(.001,a.surface.y*(1-uv.x-uv.y)+b.surface.y*uv.x+c.surface.y*uv.y)*.5;
   }else{
    // Upward-facing open lake mesh: an exit ends the submerged light path;
    // an entry starts it. Signed distances also handle entry/exit pairs.
    float t=q.CandidateTriangleRayT();bool entering=q.CandidateTriangleFrontFace();
    surfaceDistance+=entering?ray.TMax-t:t-ray.TMax;
    if(t<nearestSurface){nearestSurface=t;startsUnderwater=!entering;}
   }
   continue;
  }
  bool ownEmitter=lightEndpoint&&object.color.w>.5&&q.CandidateTriangleRayT()>limit-.12;
  if(!ownEmitter)q.CommitNonOpaqueTriangleHit();
 }
 waterDistance+=max(0,surfaceDistance+(startsUnderwater?ray.TMax:0));
 return q.CommittedStatus()==COMMITTED_NOTHING?slWaterTransmission(waterDistance):0;
}
float density(float3 p){float v=frame.z*exp(-max(p.y,0)*.35)*(.35+noise3(p*.22+float3(eyeTime.w*.035,0,0)));for(uint i=0;i<(uint)frame.w;i++)v+=.65*pow(saturate(1-length(p-smoke[i].xyz)/max(.1,smoke[i].w)),2);return v;}
float transmission(float3 p,float3 l,float distance){float sum=0;float stepSize=min(distance,24)/8;for(uint i=0;i<8;i++)sum+=density(p+l*((i+.5)*stepSize))*stepSize;return exp(-sum);}
float3 brdf(Hit h,float3 v,float3 l){SlDirectLighting response=slDirectLighting(h.n,v,l,h.albedo,h.rough,0);return response.diffuse+response.specular;}
float3 diffuseBrdf(Hit h,float3 v,float3 l){return slDirectLighting(h.n,v,l,h.albedo,h.rough,0).diffuse;}
float3 oneLight(Hit h,float3 v,uint index,out float3 diffuse){
 diffuse=0;
 float3 l,radiance;float limit;
 if(index==0){l=normalize(-sun.xyz+hemisphere(normalize(-sun.xyz))*.00465);radiance=slSolarRadiance(sun);limit=400;}
 else {uint i=index-1;float3 delta=lightPos[i].xyz-h.p;limit=length(delta);l=delta/max(.001,limit);float range=lightPos[i].w;float atten=slPointAttenuation(limit,range);float cone=slSpotCone(lightDir[i].xyz,l,lightDir[i].w,lightColor[i].w);radiance=lightColor[i].rgb*atten*cone;}
 if((dot(h.n,l)<=0&&!h.grassBlade)||max(radiance.x,max(radiance.y,radiance.z))<.00001)return 0;
 float3 offsetNormal=dot(h.n,l)>=0?h.n:-h.n;
 float3 visible=visibility(h.p,offsetNormal,l,limit,index>0);if(settings.x>=3)visible*=transmission(h.p,l,limit);
 float3 leaf=h.grassBlade?slGrassThinLight(h.albedo,h.n,v,l):0;
 diffuse=(diffuseBrdf(h,v,l)+leaf)*radiance*visible;return (brdf(h,v,l)+leaf)*radiance*visible;
}
float3 directSplit(Hit h,float3 v,bool allLights,out float3 diffuse){
 diffuse=0;
 uint count=settings.w+1;if(allLights){float3 sum=0;for(uint i=0;i<count;i++){float3 d;sum+=oneLight(h,v,i,d);diffuse+=d;}return sum;}
 // Importance-weighted reservoir: exactly one visibility ray for a sampled direct light.
 float total=0,chosenWeight=1;uint chosen=0;
 for(uint j=0;j<count;j++){float weight;if(j==0)weight=dot(slSolarRadiance(sun),float3(.2126,.7152,.0722))*max(.001,dot(h.n,-normalize(sun.xyz)));else{float3 delta=lightPos[j-1].xyz-h.p;float d=length(delta);weight=dot(lightColor[j-1].rgb,float3(.2126,.7152,.0722))*slPointAttenuation(d,lightPos[j-1].w)*max(.001,dot(h.n,delta/max(.001,d)));}weight=max(.00001,weight);total+=weight;if(random()*total<weight){chosen=j;chosenWeight=weight;}}
 float3 d;float3 value=oneLight(h,v,chosen,d)*total/chosenWeight;diffuse=d*total/chosenWeight;return value;
}
float3 direct(Hit h,float3 v,bool allLights){float3 d;return directSplit(h,v,allLights,d);}
float3 secondaryDistance(float3 p,float3 n,float3 dir,out float distance){Hit h;distance=0;if(!trace(p+n*.004,dir,400,h))return sky(dir);distance=h.distance;return h.emission+direct(h,-dir,settings.x>=3)+slEnvironmentDiffuse(h.albedo,h.n,ambient.rgb,h.ao);}

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
  if(h.kind>6.8&&h.kind<7.5){
   bool droplet=slWaterDroplet(h.kind);
   float fresnel=.0204+.9796*pow(1-saturate(dot(-dir,h.n)),5);
   float3 reflected=reflect(dir,h.n),refracted=refract(dir,h.n,1/1.333);
   if(!droplet&&reflected.y<.01)reflected=normalize(float3(reflected.x,.01,reflected.z));
   Hit bottom;float3 under=float3(.015,.055,.065),attenuation=0;
   float3 offset=droplet?h.n*.003:float3(0,.015,0);
   if(trace(h.p-offset,refracted,80,bottom,true)){
    attenuation=slWaterTransmission(droplet?h.waterThickness:bottom.distance);
    under=(bottom.emission+direct(bottom,-refracted,settings.x>=3)+slEnvironmentDiffuse(bottom.albedo,bottom.n,ambient.rgb,bottom.ao))*attenuation+float3(.008,.04,.065)*(1-attenuation);
   }else if(droplet)under=sky(refracted)*slWaterTransmission(h.waterThickness);
   float3 reflection=secondaryDistance(h.p+offset,droplet?h.n:float3(0,1,0),reflected,specDistance);
   float3 d;float3 foamLight=directSplit(h,-dir,settings.x>=3,d)+ambient.rgb*.3;
   diffuse=under*(1-fresnel)*(1-h.waterFoam)+foamLight*h.waterFoam;
   specular=reflection*fresnel*(1-h.waterFoam);emission=0;
  }else if(settings.x<4){float3 d;float3 total=directSplit(h,-dir,settings.x>=3,d);diffuse=d;specular=max(0,total-d);
   float ao=settings.x>=2?dot(visibility(h.p,h.n,hemisphere(h.n),1.5),float3(.2126,.7152,.0722)):1;diffuse+=slEnvironmentDiffuse(h.albedo,h.n,ambient.rgb,ao*h.ao);
   if(settings.x<2)specular+=slEnvironmentSpecular(h.n,-dir,ambient.rgb,h.rough,.04.xxx,h.ao,sun,weather.y);
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
