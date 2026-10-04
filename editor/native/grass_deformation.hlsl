// Resident grass state. Positions/history and normals feed every raster pass.
// One dispatch deforms every placed instance of a topology: each instance has a record in `entries`, and the
// thread groups are numbered consecutively across the instances (range.z = first group of the instance).
struct GrassVertex { float3 position; float3 normal; float4 surface; float4 history; };
struct GrassState { float4 displacement; float4 velocity; };
struct GrassBody { float4 low; float4 high; };
struct GrassEntry {
 row_major float4x4 world;
 row_major float4x4 inverseWorld;
 float4 clock; // time, dt, vertex count, body count
 uint4 settings; // reset, triangle-index base, reserved
 uint4 range; // first vertex in the pool buffers, vertex count, first thread group, reserved
};
cbuffer GrassFrame : register(b0) {
 uint4 dispatchInfo; // entry count
};
StructuredBuffer<GrassVertex> rest : register(t0);
StructuredBuffer<GrassBody> bodies : register(t1);
StructuredBuffer<uint> adjacency : register(t2);
StructuredBuffer<GrassEntry> entries : register(t3);
RWStructuredBuffer<GrassVertex> deformed : register(u0);
RWStructuredBuffer<GrassState> state : register(u1);
uint FindEntry(uint group) {
 uint low=0,high=dispatchInfo.x-1;
 while(low<high){uint middle=(low+high+1)>>1;if(entries[middle].range.z<=group)low=middle;else high=middle-1;}
 return low;
}
[numthreads(64,1,1)] void Deform(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID) {
 GrassEntry entry=entries[FindEntry(group.x)];
 uint id=(group.x-entry.range.z)*64+local.x;if(id>=entry.range.y)return;
 uint slot=entry.range.x+id;
 float4x4 world=entry.world,inverseWorld=entry.inverseWorld;float4 clock=entry.clock;uint4 settings=entry.settings;
 GrassVertex vertex=rest[id];float3 p=mul(float4(vertex.position,1),world).xyz;
 float bend=saturate(vertex.position.y/.6);bend*=bend;
 float3 target=bend*float3(.10*sin(clock.x*1.7+p.x*.4+p.z*.25)+.025*sin(clock.x*4.1+p.z),0,.07*cos(clock.x*1.3+p.z*.3));
 for(uint i=0;i<(uint)clock.w;++i){
  GrassBody box=bodies[i];
  if(p.y<box.low.y-.1||p.y>box.high.y+.1||p.x<box.low.x-.2||p.x>box.high.x+.2||p.z<box.low.z-.2||p.z>box.high.z+.2)continue;
  float4 distance=float4(p.x-box.low.x+.2,box.high.x+.2-p.x,p.z-box.low.z+.2,box.high.z+.2-p.z);
  uint face=0;for(uint f=1;f<4;++f)if(distance[f]<distance[face])face=f;
  float3 direction=face==0?float3(-1,0,0):face==1?float3(1,0,0):face==2?float3(0,0,-1):float3(0,0,1);
  target+=direction*min(.8,distance[face])*bend;
 }
 GrassState prior;if(settings.x){prior.displacement=0;prior.velocity=0;}else prior=state[slot];
 const float omega=12;float dt=max(0,clock.y),decay=exp(-omega*dt);
 float3 error=prior.displacement.xyz-target,carry=prior.velocity.xyz+omega*error;
 GrassState next=prior;if(dt>0){next.displacement=float4(target+(error+carry*dt)*decay,0);next.velocity=float4((prior.velocity.xyz-carry*(omega*dt))*decay,0);}
 float3 before=vertex.position+mul(float4(prior.displacement.xyz,0),inverseWorld).xyz;
 vertex.position+=mul(float4(next.displacement.xyz,0),inverseWorld).xyz;
 vertex.history=float4(settings.x?vertex.position:before,1);vertex.surface.w=1.37;
 deformed[slot]=vertex;state[slot]=next;
}
[numthreads(64,1,1)] void Normals(uint3 group : SV_GroupID, uint3 local : SV_GroupThreadID) {
 GrassEntry entry=entries[FindEntry(group.x)];
 uint id=(group.x-entry.range.z)*64+local.x;if(id>=entry.range.y)return;
 uint first=entry.range.x;uint slot=first+id;
 float3 normal=0;uint base=entry.range.y+1;
 for(uint k=adjacency[id];k<adjacency[id+1];++k){
  uint tri=entry.settings.y+adjacency[base+k]*3;
  float3 a=deformed[first+adjacency[tri]].position,b=deformed[first+adjacency[tri+1]].position,c=deformed[first+adjacency[tri+2]].position;
  normal+=cross(b-a,c-a);
 }
 float length2=dot(normal,normal);
 deformed[slot].normal=length2>1e-16?normal*rsqrt(length2):rest[id].normal;
}
