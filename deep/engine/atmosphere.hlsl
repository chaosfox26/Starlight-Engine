cbuffer Atmosphere : register(b0) {
 row_major float4x4 inverseViewProjection;
 float4 eyeTime; float4 sun; float4 skyDensity;
 float4 localPos[32]; float4 localColor[32]; float4 localDir[32];
 float4 smoke[16]; float4 counts;
 float4 simulation; // dt, reset, obstacle count, unused
 float4 obstacleLow[128],obstacleHigh[128],obstacleVelocity[128];
 float4 terrain[1024];
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
float2 sourceDensity(float3 p){
 float mist=skyDensity.w*exp(-max(p.y,0)*.55)*(.55+.8*noise(p*.6));float dark=0;
 for(uint i=0;i<(uint)counts.y;i++){float r=length(p-smoke[i].xyz)/max(smoke[i].w,.01);dark+=(1-smoothstep(.3,1,r))*(.25+.8*noise(p*1.4));}
 return float2(mist,dark);
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
 float3 uv=fogUV(previous);float2 seed=sourceDensity(p);
 float2 transported=fogHistory.SampleLevel(linearClamp,uv,0).xy;
 if(simulation.y>0||any(uv<0)||any(uv>1))transported=seed;
 float2 density=transported*exp(-float2(.12,.45)*simulation.x)+seed*(1-exp(-float2(.12,1.5)*simulation.x));
 fogNext[id]=float4(max(0,density),velocity.xz);
}
[numthreads(8,5,4)] void Inject(uint3 id:SV_DispatchThreadID){
 if(any(id>=uint3(160,90,64)))return;
 float3 rd=ray((id.xy+.5)/float2(160,90));float3 p=eyeTime.xyz+rd*distanceAt(id.z+.5);
 float3 uv=fogUV(p);float2 field=sourceDensity(p);
 if(all(uv>=0)&&all(uv<=1))field=fogCurrent.SampleLevel(linearClamp,uv,0).xy;
 if(p.y<=groundHeight(p.xz))field=0;
 float density=field.x,dark=field.y;
 density+=dark;
 float3 lighting=skyDensity.rgb*.12+sun.w*float3(1,.88,.73)*phase(dot(rd,-normalize(sun.xyz)))*occlusion(p,-normalize(sun.xyz),400);
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
[numthreads(8,8,1)]void Composite(uint3 id:SV_DispatchThreadID){
 uint w,h;outputColor.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;int2 p=id.xy,size=int2(w,h);float2 uv=(id.xy+.5)/float2(w,h);
 float z=depth.Load(int3(p,0));float4 world=mul(float4(uv*float2(2,-2)+float2(-1,1),z,1),inverseViewProjection);float dist=length(world.xyz/world.w-eyeTime.xyz);
 float slice=saturate(log(max(dist,.1)/.1)/log(4000));float4 fog=integral.SampleLevel(linearClamp,float3(uv,slice),0);
 float3 hdr=scene.Load(int3(p,0)).rgb;float3 c=aces(hdr*fog.a+fog.rgb);
 // Native-resolution spatial edge smoothing after the linear composite.
 float3 n=readColor(p+int2(0,-1),size),s=readColor(p+int2(0,1),size),e=readColor(p+int2(1,0),size),west=readColor(p+int2(-1,0),size);
 float lc=luma(aces(hdr)),ln=luma(n),ls=luma(s),le=luma(e),lw=luma(west),contrast=max(lc,max(max(ln,ls),max(le,lw)))-min(lc,min(min(ln,ls),min(le,lw)));
 if(contrast>.07&&fog.a>.9){float3 across=abs(ln+ls-2*lc)>=abs(le+lw-2*lc)?(n+s)*.5:(e+west)*.5;float blend=min(.35,abs((ln+ls+le+lw)*.25-lc)/max(contrast,.0001)*.55);c=lerp(c,across,blend);}
 outputColor[p]=float4(pow(c,1/2.2),1);
}
