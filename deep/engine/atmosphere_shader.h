static const char* kAtmosphereShader=R"ATMOS(
#ifndef STARLIGHT_SOLAR_RADIANCE
#define STARLIGHT_SOLAR_RADIANCE
// Shared direct sunlight in linear RGB. The longer atmospheric path near the
// horizon removes blue before red. Reference overhead transmission preserves
// existing authored intensity instead of treating it as extraterrestrial power.
// This bounded clear-sky approximation is not a spectral atmosphere simulation.
float3 slSolarRadiance(float4 sunlight){
 float elevation=-sunlight.y*rsqrt(max(dot(sunlight.xyz,sunlight.xyz),.000001));
 float mass=rsqrt(max(elevation,0)*max(elevation,0)+.0006);
 float reference=rsqrt(1.0006);
 float3 transmission=exp(-float3(.0716,.1332,.2900)*max(0,mass-reference));
 // A finite disk fades continuously through the horizon. Do not leave a
 // directional source illuminating grass, coat tips or mist from underground.
 float visibility=smoothstep(-.00465,.00465,elevation);
 return transmission*(max(0,sunlight.w)*visibility);
}
#endif

cbuffer Atmosphere : register(b0) {
 row_major float4x4 inverseViewProjection;
 float4 eyeTime; float4 sun; float4 skyDensity;
 float4 localPos[32]; float4 localColor[32]; float4 localDir[32];
 float4 smoke[16]; float4 counts;
 float4 simulation; // dt, reset, obstacle count, unused
 float4 obstacleLow[128],obstacleHigh[128],obstacleVelocity[128];
 float4 terrain[1024];
 float4 fireLow[32],fireHigh[32],fireTint[32];
 float4 smokeType[16];
 float4 post;
 float4 dustLow[24],dustHigh[24],dustTint[24];
};
Texture2D<float4> scene : register(t0);
Texture2D<float> depth : register(t1);
Texture3D<float4> cells : register(t2);
Texture3D<float4> integral : register(t3);
RWTexture3D<float4> outCells : register(u0);
RWTexture3D<float4> outIntegral : register(u1);
RWTexture2D<float4> outputColor : register(u2);
Texture3D<float4> fogHistory : register(t5);
Texture3D<float4> fogCurrent : register(t6);
RWTexture3D<float4> fogNext : register(u3);
#ifdef STARLIGHT_RT_FOG
RaytracingAccelerationStructure acceleration : register(t4);
float occlusion(float3 p,float3 l,float limit){RayDesc r;r.Origin=p;r.Direction=l;r.TMin=.01;r.TMax=max(.02,limit);RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH|RAY_FLAG_FORCE_OPAQUE> q;q.TraceRayInline(acceleration,0,255,r);while(q.Proceed()){}return q.CommittedStatus()==COMMITTED_NOTHING;}
#else
float occlusion(float3 p,float3 l,float limit){return 1;}
#endif
SamplerState linearClamp : register(s0);
float hash(float3 p){p=frac(p*.1031);p+=dot(p,p.yzx+33.33);return frac((p.x+p.y)*p.z);}
float noise(float3 p){float3 a=floor(p),f=frac(p);f=f*f*(3-2*f);return lerp(lerp(lerp(hash(a),hash(a+float3(1,0,0)),f.x),lerp(hash(a+float3(0,1,0)),hash(a+float3(1,1,0)),f.x),f.y),lerp(lerp(hash(a+float3(0,0,1)),hash(a+float3(1,0,1)),f.x),lerp(hash(a+float3(0,1,1)),hash(a+1),f.x),f.y),f.z);}
float3 ray(float2 uv){float4 p=mul(float4(uv*float2(2,-2)+float2(-1,1),.999,1),inverseViewProjection);return normalize(p.xyz/p.w-eyeTime.xyz);}
float distanceAt(float slice){return .1*pow(4000,slice/64);}
float phase(float cosine){float g=.45;return (1-g*g)/(12.56637*pow(max(.01,1+g*g-2*g*cosine),1.5));}
float3 fogUV(float3 p){return (p-float3(-16,-2,-16))/float3(32,16,32);}
float groundCell(int2 cell){cell=clamp(cell,0,63);uint index=cell.y*64+cell.x;return terrain[index/4][index%4];}
float groundHeight(float2 location){float2 uv=clamp((location+16)*2-.5,0,63);int2 cell=min(int2(uv),62);float2 f=uv-cell;return lerp(lerp(groundCell(cell),groundCell(cell+int2(1,0)),f.x),lerp(groundCell(cell+int2(0,1)),groundCell(cell+1),f.x),f.y);}
float3 sourceDensity(float3 p){
 float mist=skyDensity.w*exp(-max(p.y,0)*.55)*(.55+.8*noise(p*.6));float dark=0,chaos=0;
 for(uint i=0;i<(uint)counts.y;i++){float r=length(p-smoke[i].xyz)/max(smoke[i].w,.01);float source=(1-smoothstep(.3,1,r))*(.25+.8*noise(p*1.4));if(smokeType[i].x>.5)chaos+=source;else dark+=source;}
 return float3(mist,dark,chaos);
}
// Persistent world-space density transport. The field survives camera motion;
// obstacle exclusion and tangential flow create wakes instead of opacity flicker.
[numthreads(4,4,4)] void AdvectFog(uint3 id:SV_DispatchThreadID){
 if(any(id>=uint3(64,32,64)))return;
 float3 p=float3(-16,-2,-16)+(id+.5)*.5;
 float t=eyeTime.w;
 float3 velocity=float3(.38+.18*sin(p.z*.7+t*.4),.08+.12*sin(p.x*.55-p.z*.4+t*.3),.16+.22*cos(p.x*.6-t*.35));
 // Warm smoke rises using the previous transported density, so detached plumes
 // retain buoyancy after their emitter moves or is extinguished.
 float priorSmoke=simulation.y>0?sourceDensity(p).y:fogHistory.SampleLevel(linearClamp,fogUV(p),0).y;
 velocity.y+=min(1.5,priorSmoke*.8);
 for(uint emitter=0;emitter<(uint)counts.y;++emitter)if(smokeType[emitter].x>.5){float3 delta=p-smoke[emitter].xyz;float radius=max(smoke[emitter].w,.01);float influence=1-smoothstep(.2,1.2,length(delta)/radius);velocity+=float3(-delta.z,.2*sin(delta.x*2+t),delta.x)*(.32*influence);}
 float ground=groundHeight(p.xz);bool solid=p.y<=ground;
 float3 groundNormal=normalize(float3(groundHeight(p.xz-float2(.5,0))-groundHeight(p.xz+float2(.5,0)),1,groundHeight(p.xz-float2(0,.5))-groundHeight(p.xz+float2(0,.5))));
 velocity-=groundNormal*min(0,dot(velocity,groundNormal))*saturate(1-(p.y-ground));
 for(uint i=0;i<(uint)simulation.z;i++){
  float3 lo=obstacleLow[i].xyz,hi=obstacleHigh[i].xyz;
  if(all(p>=lo)&&all(p<=hi)){solid=true;break;}
  float3 nearest=clamp(p,lo,hi),away=p-nearest;float distance=length(away);
  if(distance<1.25&&distance>.0001){float3 n=away/distance;float influence=1-smoothstep(0,1.25,distance);velocity+=float3(-n.z,.08,n.x)*(.35*influence);
   float3 bodyVelocity=obstacleVelocity[i].xyz;
   float3 relative=velocity-bodyVelocity;velocity-=n*min(0,dot(relative,n))*influence;
   velocity+=bodyVelocity*(.55*influence)+cross(n,bodyVelocity)*(.2*influence*sin(t*4+distance*5));
  }
  float3 motion=obstacleVelocity[i].xyz;float speed=length(motion);
  if(speed>.05){float3 axis=motion/speed,center=(lo+hi)*.5,offset=p-center;float behind=-dot(offset,axis),radius=max(.4,length(hi-lo)*.5);
   float lateral=length(offset+axis*behind);float wake=saturate(1-behind/3)*saturate(1-lateral/(radius+.5));
   if(behind>0&&behind<3)velocity+=motion*(.35*wake)+cross(axis,offset)*(.4*wake);
  }

 }
 if(solid){fogNext[id]=0;return;}
 float3 previous=p-velocity*simulation.x;
 if(previous.y<=groundHeight(previous.xz))previous=p;
 // Prevent backtraces from sampling through obstacle faces.
 for(uint j=0;j<(uint)simulation.z;j++)if(all(previous>=obstacleLow[j].xyz)&&all(previous<=obstacleHigh[j].xyz))previous=p;
 float3 uv=fogUV(previous);float3 seed=sourceDensity(p);
 float3 transported=fogHistory.SampleLevel(linearClamp,uv,0).xyz;
 if(simulation.y>0||any(uv<0)||any(uv>1))transported=seed;
 float3 density=transported*exp(-float3(.12,.45,.45)*simulation.x)+seed*(1-exp(-float3(.12,1.5,1.5)*simulation.x));
 fogNext[id]=float4(max(0,density),length(velocity.xz));
}
[numthreads(8,5,4)] void Inject(uint3 id:SV_DispatchThreadID){
 if(any(id>=uint3(160,90,64)))return;
 float3 rd=ray((id.xy+.5)/float2(160,90));float3 p=eyeTime.xyz+rd*distanceAt(id.z+.5);
 float3 uv=fogUV(p);float3 field=sourceDensity(p);
 if(all(uv>=0)&&all(uv<=1))field=fogCurrent.SampleLevel(linearClamp,uv,0).xyz;
 if(p.y<=groundHeight(p.xz))field=0;
 float density=field.x,dark=field.y;
 density+=dark;
 float3 lighting=skyDensity.rgb*.12+slSolarRadiance(sun)*phase(dot(rd,-normalize(sun.xyz)))*occlusion(p,-normalize(sun.xyz),400);
 for(uint j=0;j<(uint)counts.x;j++){float3 delta=localPos[j].xyz-p;float d=length(delta);float3 l=delta/max(d,.001);float att=saturate(1-d*d/max(.01,localPos[j].w*localPos[j].w));float cone=smoothstep(localDir[j].w,localColor[j].w,dot(-l,normalize(localDir[j].xyz)));if(att*cone>.0001)lighting+=localColor[j].rgb*att*att*cone*phase(dot(rd,l))*occlusion(p,l,d-.02);}
 outCells[id]=float4(lighting*density*lerp(1,.055,saturate(dark*3)),density);
}
[numthreads(8,5,1)] void Integrate(uint3 id:SV_DispatchThreadID){
 if(id.x>=160||id.y>=90)return;float3 light=0;float transmission=1;
 for(int z=0;z<64;z++){float4 cell=cells.Load(int4(id.xy,z,0));float step=distanceAt(z+1)-distanceAt(z);float segment=exp(-cell.a*step);light+=transmission*cell.rgb*(1-segment)/max(cell.a,.00001);transmission*=segment;outIntegral[uint3(id.xy,z)]=float4(light,transmission);}
}
float3 aces(float3 c){c=max(0,c);return saturate((c*(2.51*c+.03))/(c*(2.43*c+.59)+.14));}
float3 readColor(int2 p,int2 size){return aces(scene.Load(int3(clamp(p,0,size-1),0)).rgb);}
float luma(float3 c){return dot(c,float3(.299,.587,.114));}
float fireNoise(float3 p,float footprint){
 float value=.5,weight=1;
 [unroll]for(int octave=0;octave<3;++octave){float attenuation=1-smoothstep(.35,1.2,footprint);value+=(noise(p)-.5)*weight*attenuation;p=p*2.03+float3(13.1,7.7,19.3);footprint*=2.03;weight*=.5;}
 return value;
}
float4 flameField(float3 q,float time,float footprint,float3 tint){
 float height=saturate(q.y),rise=time*1.65;
 float3 advected=q*float3(3.8,2.7,3.8)-float3(0,rise,0);
 float turbulence=fireNoise(advected,footprint*3.8);
 float2 bend=float2(sin(height*6-time*2.4),cos(height*5+time*1.9))*(.07+.14*height);
 float radius=lerp(.74,.05,pow(height,.7));
 float lateral=length(q.xz-bend);
 float envelope=1-smoothstep(radius*.4,radius+.1,lateral);
 float tongues=smoothstep(.27+height*.42,.64+height*.22,turbulence);
 float density=envelope*tongues*(1-smoothstep(.75,1,height))*smoothstep(0,.045,height)*3.8;
 float heat=saturate((1-height)*.9+turbulence*.48-lateral*.5);
 float3 radiance=lerp(float3(2.4,.08,.002),float3(9,3.3,.22),smoothstep(.25,.65,heat));
 radiance=lerp(radiance,float3(15,11,3.8),smoothstep(.78,1,heat));
 radiance*=lerp(1,max(tint,float3(.02,.02,.02))/float3(1,.35,.035),.15);
 return float4(radiance,density);
}
float chaosFlash(uint emitter){
 float age=fmod(eyeTime.w+emitter*.27,1.35);
 return smoothstep(0,.014,age)*(1-smoothstep(.065,.19,age))*(.72+.28*sin(age*190));
}
float3 chaosBoltPoint(uint index,float radius,float3 center,float cycle){
 float u=index/32.;float envelope=sin(u*3.14159265);
 float y=(noise(float3(u*5,cycle,1))-.5)*.5+(noise(float3(u*37,cycle,7))-.5)*.12;
 float z=(noise(float3(u*6,cycle,3))-.5)*.5+(noise(float3(u*41,cycle,9))-.5)*.12;
 return center+radius*float3(lerp(-.72,.65,u),lerp(.42,-.4,u)+y*envelope,z*envelope);
}
float3 chaosBranchPoint(uint index,uint branch,float radius,float3 center,float cycle){
 float3 root=chaosBoltPoint(branch==0?12:23,radius,center,cycle);
 float3 target=root+radius*float3(branch==0?-.18:.22,branch==0?-.3:.26,.17);
 float u=index/8.;float3 jitter=float3(hash(float3(index,cycle,branch+2)),hash(float3(index,cycle,branch+4)),hash(float3(index,cycle,branch+7)))-.5;
 return lerp(root,target,u)+radius*.09*sin(u*3.14159265)*jitter;
}
float3 compositeChaosBolts(float3 direction,float limit,uint height){
 float3 emission=0;
 for(uint emitter=0;emitter<(uint)counts.y;++emitter){
  if(smokeType[emitter].x<.5)continue;
  float flash=chaosFlash(emitter);
  float3 center=smoke[emitter].xyz;float radius=max(.01,smoke[emitter].w);
  float t=clamp(dot(center-eyeTime.xyz,direction),0,limit);
  if(length(eyeTime.xyz+direction*t-center)>radius*1.2)continue;
  float cycle=floor((eyeTime.w+emitter*.27)/1.35)+emitter*31;
  uint segments=flash>.001?48:0;
  for(uint segment=0;segment<segments;++segment){
   bool branch=segment>=32;uint branchIndex=(segment-32)/8,branchSegment=(segment-32)%8;
   float3 a=branch?chaosBranchPoint(branchSegment,branchIndex,radius,center,cycle):chaosBoltPoint(segment,radius,center,cycle);
   float3 b=branch?chaosBranchPoint(branchSegment+1,branchIndex,radius,center,cycle):chaosBoltPoint(segment+1,radius,center,cycle),e=b-a,w=eyeTime.xyz-a;
   float ee=dot(e,e),re=dot(direction,e),denominator=max(.000001,ee-re*re);
   float u=saturate((dot(e,w)-re*dot(direction,w))/denominator);
   float distance=clamp(dot(a+e*u-eyeTime.xyz,direction),0,limit);
   u=saturate(dot(e,eyeTime.xyz+direction*distance-a)/max(ee,.000001));
   float3 nearest=a+e*u;
   if(dot(nearest-eyeTime.xyz,direction)<=0||dot(nearest-eyeTime.xyz,direction)>=limit)continue;
   float separation=length(nearest-eyeTime.xyz-direction*distance);
   float width=radius*(branch?.0015:.0025),footprint=distance/max(height,1)*1.4;
   float filter=max(width,footprint),core=exp(-separation*separation/(filter*filter))*min(1,width/max(footprint,.00001));
   float glowWidth=radius*(branch?.016:.03),glow=exp(-separation*separation/(glowWidth*glowWidth));
   float density=fogCurrent.SampleLevel(linearClamp,fogUV(nearest),0).z;
   emission+=flash*(core*float3(12,4,20)+glow*float3(.28,.009,.6))*exp(-density*.65);
  }
  for(uint mote=0;mote<12;++mote){
   float seed=emitter*19+mote,phase=hash(float3(seed,2,9))*6.2831853+eyeTime.w*.32;
   float orbit=sqrt(hash(float3(seed,5,3)))*radius*.7;
   float lift=frac(eyeTime.w*.13+hash(float3(seed,7,1)));
   float3 location=center+float3(cos(phase)*orbit,(lift-.5)*radius*1.3,sin(phase)*orbit);
   float distance=dot(location-eyeTime.xyz,direction);if(distance<=0||distance>=limit)continue;
   float separation=length(location-eyeTime.xyz-direction*distance);
   float width=radius*(.0015+.0015*hash(float3(seed,4,8))),footprint=distance/max(height,1)*1.4;
   float filter=max(width,footprint);if(separation>filter*3)continue;
   float density=fogCurrent.SampleLevel(linearClamp,fogUV(location),0).z;
   float fade=sin(lift*3.14159265)*smoothstep(0,.04,density);
   float coverage=exp(-separation*separation/(filter*filter))*min(1,width/max(footprint,.00001));
   emission+=lerp(float3(.18,.04,.5),float3(.025,.24,.65),hash(float3(seed,8,4)))*coverage*fade;
  }
 }
 return emission;
}
float3 compositeChaos(float2 pixel,float3 direction,float limit,float3 background,uint height){
 if(simulation.w<.5)return background;
 float3 reciprocal=rcp(float3(abs(direction.x)<.000001?.000001:direction.x,abs(direction.y)<.000001?.000001:direction.y,abs(direction.z)<.000001?.000001:direction.z));
 float3 a=(float3(-16,-2,-16)-eyeTime.xyz)*reciprocal,b=(float3(16,14,16)-eyeTime.xyz)*reciprocal;
 float3 near=min(a,b),far=max(a,b);float start=max(0,max(near.x,max(near.y,near.z))),end=min(limit,min(far.x,min(far.y,far.z)));
 if(end<=start)return background;
 uint samples=(uint)clamp(ceil((end-start)/(counts.w>=2?.12:.18)),24,192);
 float step=(end-start)/samples,transmission=1;float3 emission=0;
 float jitter=frac(52.9829189*frac(dot(pixel,float2(.06711056,.00583715))))-.5;
 for(uint sample=0;sample<samples;++sample){
  float distance=start+(sample+.5+jitter)*step;float3 p=eyeTime.xyz+direction*distance;
  float density=fogCurrent.SampleLevel(linearClamp,fogUV(p),0).z;
  if(density<.0005||p.y<=groundHeight(p.xz))continue;
  float time=eyeTime.w;
  float3 drift=float3(time*.14,-time*.09,time*.06);
  float3 warp=float3(noise(p*.47+drift),noise(p*.51-drift+17),noise(p*.43+drift+31))-.5;
  float3 q=p*3.2+warp*2.4-drift;
  float footprint=max(step*.3,distance/max(height,1)*2);
  float cloud=.5,weight=.6;float3 octavePoint=q;float filter=footprint*3.2;
  [unroll]for(int octave=0;octave<4;++octave){cloud+=(noise(octavePoint)-.5)*weight*(1-smoothstep(.35,1,filter));octavePoint=octavePoint*2.03+float3(13.1,7.7,19.3);weight*=.5;filter*=2.03;}
  float pockets=smoothstep(.3,.68,noise(q*.51+float3(17,3,7)));
  float detail=smoothstep(.4,.65,cloud);
  float extinction=density*(.008+detail*.9);
  float blue=smoothstep(.28,.73,noise(q*.61+float3(2,11,5)));
  float3 color=lerp(float3(.72,.022,1.7),float3(.018,.3,2.3),blue);
  float glow=pow(saturate(detail),3)*pockets;
  float3 radiance=color*(.003+glow*.3)+skyDensity.rgb*.015;
  for(uint emitter=0;emitter<(uint)counts.y;++emitter)if(smokeType[emitter].x>.5){float flash=chaosFlash(emitter);float distance=length(p-smoke[emitter].xyz)/max(.01,smoke[emitter].w);radiance+=float3(.8,.04,1.5)*flash*saturate(1-distance)*detail;}
  float segment=exp(-extinction*step);
  emission+=transmission*(1-segment)*radiance;transmission*=segment;
  if(transmission<.006)break;
 }
 return background*transmission+emission+compositeChaosBolts(direction,limit,height);
}
float3 compositeFire(float2 uv,float3 direction,float limit,float3 background,uint width,uint height){
 float entry[32],exit[32];uint order[32],count=0;
 float3 reciprocal=rcp(float3(abs(direction.x)<.000001?.000001:direction.x,abs(direction.y)<.000001?.000001:direction.y,abs(direction.z)<.000001?.000001:direction.z));
 for(uint volume=0;volume<(uint)counts.z;++volume){
  float3 a=(fireLow[volume].xyz-eyeTime.xyz)*reciprocal,b=(fireHigh[volume].xyz-eyeTime.xyz)*reciprocal;
  float3 near=min(a,b),far=max(a,b);float start=max(0,max(near.x,max(near.y,near.z))),end=min(limit,min(far.x,min(far.y,far.z)));
  if(end<=start)continue;
  uint at=count;while(at>0&&entry[at-1]<start){entry[at]=entry[at-1];exit[at]=exit[at-1];order[at]=order[at-1];--at;}
  entry[at]=start;exit[at]=end;order[at]=volume;++count;
 }
 uint samples=counts.w>=2?48:24;
 for(uint sorted=0;sorted<count;++sorted){
  uint volume=order[sorted];float3 lo=fireLow[volume].xyz,hi=fireHigh[volume].xyz,extent=max(hi-lo,.0001);
  float step=(exit[sorted]-entry[sorted])/samples,transmission=1;float3 emission=0;
  for(uint sample=0;sample<samples;++sample){
   float distance=entry[sorted]+(sample+.5)*step;float3 p=eyeTime.xyz+direction*distance;
   float3 q=(p-lo)/extent;q.xz=q.xz*2-1;
   float footprint=distance/max(height,1)*2/max(.001,min(extent.x,min(extent.y,extent.z)));
   float4 flame=flameField(q,eyeTime.w,footprint,fireTint[volume].rgb);
   float segment=exp(-flame.a*step/max(extent.y,.001));
   float slice=saturate(log(max(distance,.1)/.1)/log(4000));float atmosphere=integral.SampleLevel(linearClamp,float3(uv,slice),0).a;
   emission+=transmission*(1-segment)*flame.rgb*atmosphere;transmission*=segment;
  }
  background=background*transmission+emission;
 }
 return background;
}
// Mineral dust scatters scene light and attenuates the background; it is not an
// emissive smoke sprite. The finite box only bounds integration: its density has
// a smooth spherical edge and filtered internal eddies.
float3 compositeDust(float2 uv,float3 direction,float limit,float3 background,uint height){
 float entry[24],exit[24];uint order[24],count=0;
 float3 reciprocal=rcp(float3(abs(direction.x)<.000001?.000001:direction.x,abs(direction.y)<.000001?.000001:direction.y,abs(direction.z)<.000001?.000001:direction.z));
 for(uint volume=0;volume<(uint)post.y;++volume){
  float3 a=(dustLow[volume].xyz-eyeTime.xyz)*reciprocal,b=(dustHigh[volume].xyz-eyeTime.xyz)*reciprocal;
  float3 near=min(a,b),far=max(a,b);float start=max(0,max(near.x,max(near.y,near.z))),end=min(limit,min(far.x,min(far.y,far.z)));
  if(end<=start)continue;
  uint at=count;while(at>0&&entry[at-1]<start){entry[at]=entry[at-1];exit[at]=exit[at-1];order[at]=order[at-1];--at;}
  entry[at]=start;exit[at]=end;order[at]=volume;++count;
 }
 uint samples=counts.w>=2?28:16;
 for(uint sorted=0;sorted<count;++sorted){
  uint volume=order[sorted];float3 lo=dustLow[volume].xyz,hi=dustHigh[volume].xyz,extent=max(hi-lo,.0001),center=(lo+hi)*.5;
  float step=(exit[sorted]-entry[sorted])/samples,transmission=1;float3 scattering=0;
  for(uint sample=0;sample<samples;++sample){
   float distance=entry[sorted]+(sample+.5)*step;float3 p=eyeTime.xyz+direction*distance;
   float3 q=(p-center)*2/extent;float envelope=1-smoothstep(.25,1,length(q));if(envelope<=0)continue;
   bool solid=false;for(uint obstacle=0;obstacle<(uint)simulation.z;++obstacle)if(all(p>=obstacleLow[obstacle].xyz)&&all(p<=obstacleHigh[obstacle].xyz)){solid=true;break;}if(solid)continue;
   float footprint=distance/max(height,1)*2;
   float detail=fireNoise(p*4-float3(.15,.23,.08)*eyeTime.w,footprint*4);
   float density=envelope*(.45+.7*detail)*dustTint[volume].a*2.4/max(.001,extent.x);
   float3 lighting=skyDensity.rgb*.4+slSolarRadiance(sun)*phase(dot(direction,-normalize(sun.xyz)))*4;
   for(uint light=0;light<(uint)counts.x;++light){float3 delta=localPos[light].xyz-p;float d=length(delta);float3 l=delta/max(d,.001);float att=saturate(1-d*d/max(.01,localPos[light].w*localPos[light].w));float cone=smoothstep(localDir[light].w,localColor[light].w,dot(-l,normalize(localDir[light].xyz)));lighting+=localColor[light].rgb*att*att*cone*phase(dot(direction,l))*4;}
   float segment=exp(-density*step);float slice=saturate(log(max(distance,.1)/.1)/log(4000));float atmosphere=integral.SampleLevel(linearClamp,float3(uv,slice),0).a;
   scattering+=transmission*(1-segment)*dustTint[volume].rgb*lighting*atmosphere;transmission*=segment;
  }
  background=background*transmission+scattering;
 }
 return background;
}
// Overlapping flame and mineral clouds must integrate in ray order. Separate
// whole-volume layers would let a rear cloud cover a nearer flame. Split at all
// entry/exit boundaries, then integrate the summed extinction/scattering field.
float3 compositeTransient(float2 uv,float3 direction,float limit,float3 background,uint width,uint height){
 if(post.y<.5)return compositeFire(uv,direction,limit,background,width,height);
 if(counts.z<.5)return compositeDust(uv,direction,limit,background,height);
 float3 reciprocal=rcp(float3(abs(direction.x)<.000001?.000001:direction.x,abs(direction.y)<.000001?.000001:direction.y,abs(direction.z)<.000001?.000001:direction.z));
 float transmission=1;float3 radiance=0;float start=0;
 // Find the next crossing on demand. Large dynamically indexed arrays spill
 // into per-pixel GPU scratch storage even when that pixel has no cloud.
 [loop]for(uint interval=0;interval<112;++interval){
  float finish=1e20,minStep=1e20;bool active=false;
  [loop]for(uint category=0;category<2;++category){uint total=category==0?(uint)counts.z:(uint)post.y;
   [loop]for(uint v=0;v<total;++v){
    float3 lo=category==0?fireLow[v].xyz:dustLow[v].xyz,hi=category==0?fireHigh[v].xyz:dustHigh[v].xyz;
    float3 a=(lo-eyeTime.xyz)*reciprocal,b=(hi-eyeTime.xyz)*reciprocal,near=min(a,b),far=max(a,b);
    float entry=max(0,max(near.x,max(near.y,near.z))),exit=min(limit,min(far.x,min(far.y,far.z)));if(exit<=entry)continue;
    if(entry>start+.000001)finish=min(finish,entry);if(exit>start+.000001)finish=min(finish,exit);
    if(entry<=start+.000001&&exit>start+.000001){active=true;float3 extent=hi-lo;minStep=min(minStep,max(.001,min(extent.x,min(extent.y,extent.z)))/(counts.w>=2?48:24));}
   }
  }
  if(finish>1e19)break;if(!active||finish-start<.000001){start=finish;continue;}
  uint samples=(uint)clamp(ceil((finish-start)/minStep),1,96);float step=(finish-start)/samples;
  [loop]for(uint sample=0;sample<samples;++sample){
   float distance=start+(sample+.5)*step;float3 p=eyeTime.xyz+direction*distance,emission=0;float extinction=0;
   [loop]for(uint category=0;category<2;++category){uint total=category==0?(uint)counts.z:(uint)post.y;
    [loop]for(uint v=0;v<total;++v){float3 boundsLow=category==0?fireLow[v].xyz:dustLow[v].xyz,boundsHigh=category==0?fireHigh[v].xyz:dustHigh[v].xyz;if(any(p<boundsLow)||any(p>boundsHigh))continue;
    if(category==0){float3 extent=max(fireHigh[v].xyz-fireLow[v].xyz,.0001);float3 q=(p-fireLow[v].xyz)/extent;q.xz=q.xz*2-1;float footprint=distance/max(height,1)*2/max(.001,min(extent.x,min(extent.y,extent.z)));float4 flame=flameField(q,eyeTime.w,footprint,fireTint[v].rgb);float density=flame.a/max(extent.y,.001);extinction+=density;emission+=flame.rgb*density;}
    else {float3 lo=dustLow[v].xyz,hi=dustHigh[v].xyz,extent=max(hi-lo,.0001),q=(p-(lo+hi)*.5)*2/extent;float envelope=1-smoothstep(.25,1,length(q));if(envelope<=0)continue;
     bool solid=false;[loop]for(uint obstacle=0;obstacle<(uint)simulation.z;++obstacle)if(all(p>=obstacleLow[obstacle].xyz)&&all(p<=obstacleHigh[obstacle].xyz)){solid=true;break;}if(solid)continue;
     float detail=fireNoise(p*4-float3(.15,.23,.08)*eyeTime.w,distance/max(height,1)*8);float density=envelope*(.45+.7*detail)*dustTint[v].a*2.4/max(.001,extent.x);
     float3 lighting=skyDensity.rgb*.4+slSolarRadiance(sun)*phase(dot(direction,-normalize(sun.xyz)))*4;
     [loop]for(uint light=0;light<(uint)counts.x;++light){float3 delta=localPos[light].xyz-p;float d=length(delta);float3 l=delta/max(d,.001);float att=saturate(1-d*d/max(.01,localPos[light].w*localPos[light].w));float cone=smoothstep(localDir[light].w,localColor[light].w,dot(-l,normalize(localDir[light].xyz)));lighting+=localColor[light].rgb*att*att*cone*phase(dot(direction,l))*4;}
     extinction+=density;emission+=dustTint[v].rgb*lighting*density;
    }
   }
   }
   float segment=exp(-extinction*step),slice=saturate(log(max(distance,.1)/.1)/log(4000)),atmosphere=integral.SampleLevel(linearClamp,float3(uv,slice),0).a;
   radiance+=transmission*(1-segment)*emission/max(extinction,.00001)*atmosphere;transmission*=segment;
  }
  start=finish;
 }
 return background*transmission+radiance;
}
[numthreads(8,8,1)]void Composite(uint3 id:SV_DispatchThreadID){
 uint w,h;outputColor.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;int2 p=id.xy,size=int2(w,h);float2 uv=(id.xy+.5)/float2(w,h);
 float z=depth.Load(int3(p,0));float4 world=mul(float4(uv*float2(2,-2)+float2(-1,1),z,1),inverseViewProjection);float dist=length(world.xyz/world.w-eyeTime.xyz);
 float slice=saturate(log(max(dist,.1)/.1)/log(4000));float4 fog=integral.SampleLevel(linearClamp,float3(uv,slice),0);
 float3 hdr=scene.Load(int3(p,0)).rgb,base=hdr*fog.a+fog.rgb,clouds=compositeChaos(id.xy,ray(uv),dist,base,h),combined=compositeTransient(uv,ray(uv),dist,clouds,w,h);
 if(post.x>.5){outputColor[p]=float4(max(combined,0),1);return;}
 float3 c=aces(combined);
 // Native-resolution spatial edge smoothing after the linear composite.
 float3 n=readColor(p+int2(0,-1),size),s=readColor(p+int2(0,1),size),e=readColor(p+int2(1,0),size),west=readColor(p+int2(-1,0),size);
 float lc=luma(aces(hdr)),ln=luma(n),ls=luma(s),le=luma(e),lw=luma(west),contrast=max(lc,max(max(ln,ls),max(le,lw)))-min(lc,min(min(ln,ls),min(le,lw)));
 if(max(abs(combined.x-base.x),max(abs(combined.y-base.y),abs(combined.z-base.z)))<.001&&contrast>.07&&fog.a>.9){float3 across=abs(ln+ls-2*lc)>=abs(le+lw-2*lc)?(n+s)*.5:(e+west)*.5;float blend=min(.35,abs((ln+ls+le+lw)*.25-lc)/max(contrast,.0001)*.55);c=lerp(c,across,blend);}
 outputColor[p]=float4(pow(c,1/2.2),1);
}

)ATMOS";
