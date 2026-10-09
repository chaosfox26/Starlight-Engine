#ifdef STARLIGHT_CMAKE_RENDERER
#include "event_log.h"
#include <render_device_editor.h>
#else
#include "render_device_editor.h"
#endif
#include "../../deep/engine/atmosphere.h"
#include "../../deep/src/render_limits.hpp"
#include "../../deep/engine/chaos_lightning.h"
#include <cstring>
#include <chrono>
#include <fstream>
#include <unordered_set>
#include "../../deep/engine/surface_textures.h"
#include "../../deep/engine/dxr.h"
#include "../../deep/engine/hdr_post.h"
#include "../../deep/engine/sun_shadows.h"
#include "../../deep/engine/point_shadows.h"
#include "../../deep/engine/grass_deformation.h"

struct Reader {
 const uint8_t* p; size_t left;
 template<class T>T get(){if(left<sizeof(T))throw std::runtime_error("Incomplete viewport scene");T v;memcpy(&v,p,sizeof(v));p+=sizeof(v);left-=sizeof(v);return v;}
 template<class T>std::vector<T> array(uint32_t n){if(n>left/sizeof(T))throw std::runtime_error("Invalid viewport array");std::vector<T> v(n);memcpy(v.data(),p,n*sizeof(T));p+=n*sizeof(T);left-=n*sizeof(T);return v;}
};
struct Part { XMFLOAT3 shadowLow{},shadowHigh{},fogLow{},fogHigh{};bool fogSolid=false;std::shared_ptr<Mesh> source;GpuMesh gpu; uint64_t meshKey=0,motionKey=0; bool streaming=false;std::vector<uint64_t> vertexIdentities; XMFLOAT3 color; float x,z,size,speed; float emissive=0,baseY=0;int effect=0; XMFLOAT4X4 local{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; std::vector<XMFLOAT2> route; };
// Previous/current vertex positions keyed by global motion identity. Open addressing with a generation stamp:
// clear() is O(1) and a frame of ~1M moving vertices does no per-vertex allocation.
struct VertexHistory {
 struct Slot{uint64_t key=0;float x=0,y=0,z=0;uint32_t gen=0;};
 std::vector<Slot> slots;size_t mask=0,count=0;uint32_t gen=1;
 static size_t slot_hash(uint64_t key){key^=key>>33;key*=0xff51afd7ed558ccdull;key^=key>>33;key*=0xc4ceb9fe1a85ec53ull;key^=key>>33;return size_t(key);}
 void clear(){count=0;if(++gen==0){for(auto& s:slots)s.gen=0;gen=1;}}
 const Slot* find(uint64_t key)const{
  if(slots.empty())return nullptr;
  for(size_t i=slot_hash(key)&mask;;i=(i+1)&mask){const Slot& s=slots[i];if(s.gen!=gen)return nullptr;if(s.key==key)return &s;}
 }
 void grow(){
  std::vector<Slot> old;old.swap(slots);const size_t capacity=old.empty()?4096:old.size()*2;
  slots.assign(capacity,Slot{});mask=capacity-1;const uint32_t oldGen=gen;gen=1;count=0;
  for(const auto& s:old)if(s.gen==oldGen)place(s.key,s.x,s.y,s.z);
 }
 void place(uint64_t key,float x,float y,float z){
  size_t i=slot_hash(key)&mask;
  while(slots[i].gen==gen){if(slots[i].key==key){slots[i].x=x;slots[i].y=y;slots[i].z=z;return;}i=(i+1)&mask;}
  slots[i]={key,x,y,z,gen};++count;
 }
 void set(uint64_t key,const XMFLOAT3& p){if(slots.empty()||(count+1)*10>slots.size()*7)grow();place(key,p.x,p.y,p.z);}
 void prefetch(uint64_t key)const{if(!slots.empty())_mm_prefetch(reinterpret_cast<const char*>(&slots[slot_hash(key)&mask]),_MM_HINT_T0);}
 void swap(VertexHistory& other){slots.swap(other.slots);std::swap(mask,other.mask);std::swap(count,other.count);std::swap(gen,other.gen);}
};
struct Viewport {
 SurfaceTextures surfaces;DxrPass rt;std::vector<DxrItem> rtItems;int shadowQuality=-1;bool freeze=false,externalEffectClock=false,screenReflections=true;float frozenTime=0,effectClock=0;
 float visualTime()const{return freeze?frozenTime:externalEffectClock?effectClock:std::chrono::duration<float>(std::chrono::steady_clock::now()-started).count();}
 bool geometryFresh=false;
 VertexHistory previousVertices,currentVertices;
 VertexHistory staticVertices; // vertex identity -> position of parts that never move their vertices (GPU grass rest meshes), recorded once
 std::unordered_map<uint64_t,std::pair<const void*,uint64_t>> staticRecorded; // first vertex identity -> (mesh address, content fingerprint)
 bool externalFogObstacles=false;
 bool resizeStale=false,postStale=false; // set while a resize or glow rebuild is unfinished, so an identical retry runs again instead of reporting success
 AtmospherePass atmosphere;HdrPostPass post;SunShadows sunShadows;PointShadows pointShadows;FrameCB frameLights{};float hour=15,cycle=0,dayLength=180,fogDensity=.018f;std::vector<XMFLOAT4> smoke;std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
 std::unique_ptr<RenderDevice> renderer;
 std::vector<uint64_t> telegraphIdentities;
 std::vector<Part> parts;std::vector<SpotLight> localLights;
 GrassDeformation grass;
 XMFLOAT4 weather{};std::array<XMFLOAT4,64> weatherLow{},weatherHigh{};

 struct TelegraphInstance {float x,y,z,aimX,aimZ,range,halfAngle,progress,r,g,b;};
 struct TelegraphTemplate {float halfAngle;int rings;GpuMesh gpu;};
 std::vector<TelegraphInstance> telegraphs;
 std::vector<TelegraphTemplate> telegraphTemplates;
 UINT64 telegraphBytes=0;
 struct SideAllocator {ComPtr<ID3D12CommandAllocator> allocator;UINT64 fence=0;};
 struct SideStaging {std::vector<RenderDevice::UploadStorage> storage;UINT64 fence=0;};
 std::vector<SideAllocator> sideAllocators;std::vector<SideStaging> sideStaging;size_t sideAllocator=0,sideStagingBase=0;bool sideOpen=false;
 void reapSideStaging(){
  if(sideStaging.empty())return;auto& d=*renderer;const UINT64 completed=d.fence->GetCompletedValue();
  std::vector<RenderDevice::UploadStorage> held;held.swap(d.uploadKeepAlive);
  std::erase_if(sideStaging,[&](SideStaging& item){if(item.fence>completed)return false;for(auto& storage:item.storage)d.uploadKeepAlive.push_back(std::move(storage));return true;});
  d.retireUploads();d.uploadKeepAlive.swap(held);
 }
 void beginSide(){
  auto& d=*renderer;reapSideStaging();const UINT64 completed=d.fence->GetCompletedValue();
  size_t pick=sideAllocators.size();for(size_t i=0;i<sideAllocators.size();++i)if(sideAllocators[i].fence<=completed){pick=i;break;}
  if(pick==sideAllocators.size()){
   if(sideAllocators.size()<8){SideAllocator made;d.must(d.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&made.allocator)),"create upload command allocator");sideAllocators.push_back(std::move(made));}
   else{pick=0;for(size_t i=1;i<sideAllocators.size();++i)if(sideAllocators[i].fence<sideAllocators[pick].fence)pick=i;d.waitFence(sideAllocators[pick].fence);}
  }
  d.must(sideAllocators[pick].allocator->Reset(),"reset upload command allocator");d.must(d.cmd->Reset(sideAllocators[pick].allocator.Get(),nullptr),"begin upload commands");
  sideAllocator=pick;sideStagingBase=d.uploadKeepAlive.size();sideOpen=true;
 }
 void submitSide(){
  auto& d=*renderer;d.must(d.cmd->Close(),"close upload commands");sideOpen=false;ID3D12CommandList* lists[]={d.cmd.Get()};d.queue->ExecuteCommandLists(1,lists);
  const UINT64 value=++d.fenceVal;sideAllocators[sideAllocator].fence=value;
  SideStaging held;held.fence=value;for(size_t i=sideStagingBase;i<d.uploadKeepAlive.size();++i)held.storage.push_back(std::move(d.uploadKeepAlive[i]));d.uploadKeepAlive.erase(d.uploadKeepAlive.begin()+sideStagingBase,d.uploadKeepAlive.end());
  if(!held.storage.empty())sideStaging.push_back(std::move(held));
  d.must(d.queue->Signal(d.fence.Get(),value),"signal upload completion");
 }
 void abandonSide(){
  if(!sideOpen)return;sideOpen=false;auto& d=*renderer;d.cmd->Close();if(sideStagingBase<d.uploadKeepAlive.size())d.uploadKeepAlive.erase(d.uploadKeepAlive.begin()+sideStagingBase,d.uploadKeepAlive.end());
 }
 struct SideScope {
  Viewport& viewport;bool active=false;
  explicit SideScope(Viewport& v):viewport(v){}
  void begin(){viewport.beginSide();active=true;}
  void submit(){active=false;viewport.submitSide();}
  ~SideScope(){if(active)viewport.abandonSide();}
  SideScope(const SideScope&)=delete;SideScope& operator=(const SideScope&)=delete;
 };
 void prepareTelegraph(float halfAngle,int rings){
  for(const auto& t:telegraphTemplates)if(t.halfAngle==halfAngle && t.rings==rings)return;
  auto& d=*renderer;
  if(telegraphTemplates.size()>=64)throw std::runtime_error("Resident effect template capacity exceeded");
  Mesh mesh;
  for(int i=1;i<=24;++i){float a=-halfAngle+2*halfAngle*(i-1)/24,b=-halfAngle+2*halfAngle*i/24;
   for(int j=0;j<rings;++j){float inner=float(j)/rings,outer=float(j+1)/rings;UINT k=UINT(mesh.verts.size());
    for(auto point:{XMFLOAT2(sinf(a)*inner,-cosf(a)*inner),XMFLOAT2(sinf(a)*outer,-cosf(a)*outer),XMFLOAT2(sinf(b)*outer,-cosf(b)*outer),XMFLOAT2(sinf(b)*inner,-cosf(b)*inner)})
     mesh.verts.push_back({point.x,0,point.y,0,1,0,1,1,1,0});
    for(UINT index:{0u,1u,2u,0u,2u,3u,2u,1u,0u,3u,2u,0u})mesh.indices.push_back(k+index);
   }
  }
  SideScope side(*this);side.begin();
  auto gpu=d.uploadMesh(mesh);side.submit();
  telegraphBytes+=starlight_resource_desc(gpu.vb).Width+starlight_resource_desc(gpu.ib).Width;
  telegraphTemplates.push_back({halfAngle,rings,std::move(gpu)});
 }
 struct RetiredStream {GpuMesh mesh;UINT64 fence=0;UINT64 vbWidth=0,ibWidth=0;};   // widths are cached so searching the spare list never calls GetDesc
 size_t streamDemand=0,streamCreated=0,streamReused=0,streamDemandPerInstall=0;double streamMs=0,streamCreateMs=0;
 std::vector<RetiredStream> streams;
 // Rigid meshes (rubble fragments, sparks) uploaded once and drawn many times with a per-frame pose. Their motion
 // history comes from the renderer's per-key model history; they carry no per-vertex history.
 struct ResidentMesh {std::shared_ptr<Mesh> source;GpuMesh gpu;XMFLOAT3 low{},high{};};
 std::unordered_map<uint64_t,ResidentMesh> residentMeshes;
 std::vector<std::shared_ptr<Mesh>> sourceGraveyard; // CPU copies of released resident meshes, freed a few per call so a despawn never frees them all in one frame
 std::vector<Part> instanceParts;
 struct RetiredResident {GpuMesh gpu;UINT64 fence=0;};
 std::vector<RetiredResident> retiredResident;
 static constexpr size_t kRetiredResidentCap=16384;uint64_t residentDrained=0,residentMaxPending=0,residentForcedStalls=0,residentForcedMicros=0;
 bool streamsWarmed=false;
 // Streaming meshes (dust puffs, sparks) appear in bursts; creating an upload buffer costs about 0.2 ms each, so a pool of small spares is created while the scene loads.
 void warmStreams(){
  if(streamsWarmed)return;streamsWarmed=true;auto& d=*renderer;
  auto buffer=[&](UINT64 bytes){ComPtr<ID3D12Resource> resource;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=std::max<UINT64>(256,bytes);desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.must(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&resource)),"create transient geometry buffer");return resource;};
  for(int i=0;i<96;++i){GpuMesh spare;spare.vb=buffer(16384);spare.ib=buffer(4096);streams.push_back({spare,0,16384,4096});}
 }
 GpuMesh streamMesh(const Mesh& mesh){
  auto& d=*renderer;GpuMesh gpu;const UINT64 vb=mesh.verts.size()*sizeof(Vertex),ib=mesh.indices.size()*sizeof(uint32_t);
  const double streamStart=starlight_diag::events_enabled()?starlight_diag::evnow():0;struct StreamTimer{Viewport* v;double start;double* createMs;~StreamTimer(){if(start>0)v->streamMs+=starlight_diag::evnow()-start;}} streamTimer{this,streamStart,&streamCreateMs};
  const UINT64 completed=d.fence->GetCompletedValue();
  ++streamDemand;for(auto it=streams.begin();it!=streams.end();++it)if(it->fence<=completed && it->vbWidth>=vb && it->ibWidth>=ib){gpu=std::move(it->mesh);streams.erase(it);++streamReused;break;}
  auto buffer=[&](UINT64 bytes){ComPtr<ID3D12Resource> resource;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=std::max<UINT64>(256,bytes);desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.must(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&resource)),"create transient geometry buffer");return resource;};
  if(!gpu.vb){const double c0=streamStart>0?starlight_diag::evnow():0;gpu.vb=buffer(vb);gpu.ib=buffer(ib);++streamCreated;if(c0>0){streamCreateMs+=starlight_diag::evnow()-c0;starlight_diag::evlog("scr",double(vb),double(ib),starlight_diag::evnow()-c0);}}
  auto write=[&](ID3D12Resource* resource,const void* data,size_t bytes){void* mapped=nullptr;D3D12_RANGE read{0,0};d.must(resource->Map(0,&read,&mapped),"map transient geometry");memcpy(mapped,data,bytes);D3D12_RANGE written{0,bytes};resource->Unmap(0,&written);};
  write(gpu.vb.Get(),mesh.verts.data(),size_t(vb));write(gpu.ib.Get(),mesh.indices.data(),size_t(ib));
  gpu.indexCount=UINT(mesh.indices.size());gpu.vbv={gpu.vb->GetGPUVirtualAddress(),UINT(vb),UINT(sizeof(Vertex))};gpu.ibv={gpu.ib->GetGPUVirtualAddress(),UINT(ib),DXGI_FORMAT_R32_UINT};
  // Only completed spare buffers may be evicted. Live draws retain their resources.
  // Spares must cover the meshes drawn per frame times the frames in flight, or every frame would destroy and recreate buffers.
  const size_t spareLimit=std::max<size_t>(160,streamDemandPerInstall*4);UINT64 retained=0;for(auto it=streams.begin();it!=streams.end();){UINT64 bytes=it->vbWidth+it->ibWidth;if(it->fence<=completed && (streams.size()>spareLimit || retained+bytes>32ull*1024*1024))it=streams.erase(it);else{retained+=bytes;++it;}}
  return gpu;
 }
 float previousEffectTime=0;bool hasEffectTime=false;
 double seconds=0;uint64_t uploads=0,transientIdentity=0;
 // Resident static scene: parts [0,baseCount) were installed from one base packet and are
 // kept (GPU meshes, decoded sources, bounds) while only the tail is replaced per update.
 size_t baseCount=0;uint64_t baseToken=0;
 struct SceneStats{double parse=0,hash=0,bounds=0,upload=0,tail=0,total=0;uint64_t calls=0,deltaCalls=0,bytes=0,vertices=0;} sceneStats;
 HWND window=nullptr;
 double drawSegments[5]{};
 void draw(){
 using DrawClock=std::chrono::steady_clock;const auto segT0=DrawClock::now();
 const size_t ownParts=parts.size();
  parts.insert(parts.end(),instanceParts.begin(),instanceParts.end());
  struct RestoreParts{std::vector<Part>& list;size_t count;~RestoreParts(){list.resize(count);}} restoreParts{parts,ownParts};
  auto& d=*renderer;surfaces.bind(d);currentVertices.clear();rtItems.clear();atmosphere.fires.clear();atmosphere.dust.clear();if(!externalFogObstacles){atmosphere.obstacleLow.clear();atmosphere.obstacleHigh.clear();atmosphere.obstacleVelocity.clear();}
  float time=visualTime();
  grass.begin(d,time);
  float angle=(hour+(cycle>0?time/dayLength*24:0)-6)*3.14159265f/12;float elevation=sinf(angle),day=std::clamp(elevation*2,0.f,1.f);
  FrameCB lights{};XMStoreFloat4x4(&lights.inverseViewProjection,XMMatrixInverse(nullptr,d.viewProjNoJit));XMStoreFloat4x4(&lights.viewProjection,d.viewProjNoJit);lights.motionTime={time,hasEffectTime?previousEffectTime:time,geometryFresh?1.f:0.f,screenReflections?1.f:0.f};previousEffectTime=time;hasEffectTime=true;lights.lightDir=XMFLOAT4(cosf(angle),-elevation,-.35f,day*3.8f);lights.ambient=XMFLOAT4(.025f+day*.355f,.018f+day*.442f,.07f+day*.50f,.16f+day*.12f);lights.camPos=XMFLOAT4(d.camWorld.x,d.camWorld.y,d.camWorld.z,1);lights.fog=XMFLOAT4(.12f,.18f,.24f,fogDensity);
  for(size_t i=0;i<localLights.size()&&i<32;i++){lights.spots[i]=localLights[i];if(lights.spots[i].dir.w<-2.5f){float pulse=.72f+.28f*sinf(time*37+floorf(time*19)*3.1f);lights.spots[i].color.x*=pulse;lights.spots[i].color.y*=pulse;lights.spots[i].color.z*=pulse;}}
  lights.weather=weather;for(size_t i=0;i<64;++i){lights.weatherLow[i]=weatherLow[i];lights.weatherHigh[i]=weatherHigh[i];}
  float storm=weather.y;lights.lightDir.w*=1-.72f*storm;
  lights.ambient.x*=1-.32f*storm;lights.ambient.y*=1-.30f*storm;lights.ambient.z*=1-.24f*storm;
  struct LiquidDraw {const GpuMesh* mesh;XMMATRIX world;XMFLOAT3 color;float emissive;uint64_t key;float distance=0;};std::vector<LiquidDraw> liquids,solids;std::vector<SunShadows::Item> casters;
  size_t partIndex=0;for(const auto& p:parts){auto grassKey=GrassDeformation::identity(p.motionKey,partIndex++);if(p.effect==4)continue;
   float x=p.x,z=p.z,yaw=0;double remaining=seconds*p.speed;
   for(const auto& q:p.route){double dx=q.x-x,dz=q.y-z,len=sqrt(dx*dx+dz*dz);if(len>1e-8)yaw=(float)atan2(dx,dz);if(len>remaining&&len>0){x+=(float)(dx*remaining/len);z+=(float)(dz*remaining/len);break;}remaining-=len;x=q.x;z=q.y;}
   if(seconds<=0)yaw=0;
   auto transform=XMLoadFloat4x4(&p.local)*XMMatrixScaling(p.size,p.size,p.size)*XMMatrixRotationY(yaw)*XMMatrixTranslation(x,0,z);
   if(!p.source->verts.empty()&&p.source->verts.front().kind>=13&&p.source->verts.front().kind<14){
    if(atmosphere.dust.size()>=24)throw std::runtime_error("Dust volume capacity exceeded");
    XMFLOAT3 lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};
    for(const auto& vertex:p.source->verts){XMFLOAT3 q;XMStoreFloat3(&q,XMVector3TransformCoord(XMVectorSet(vertex.px,vertex.py,vertex.pz,1),transform));lo.x=std::min(lo.x,q.x);lo.y=std::min(lo.y,q.y);lo.z=std::min(lo.z,q.z);hi.x=std::max(hi.x,q.x);hi.y=std::max(hi.y,q.y);hi.z=std::max(hi.z,q.z);}
    {static const bool noDust=std::getenv("STARLIGHT_DIAG_NO_DUST")!=nullptr; /* diagnostic */ if(noDust)continue;}
    atmosphere.dust.push_back({{lo.x,lo.y,lo.z,0},{hi.x,hi.y,hi.z,0},{p.color.x,p.color.y,p.color.z,(p.source->verts.front().kind-13.f)/.4f}});continue;
   }
   if(!p.source->verts.empty()&&p.source->verts.front().kind==12){
    if(atmosphere.fires.size()>=32)throw std::runtime_error("Fire volume capacity exceeded");
    XMFLOAT3 lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};
    for(const auto& vertex:p.source->verts){XMFLOAT3 q;XMStoreFloat3(&q,XMVector3TransformCoord(XMVectorSet(vertex.px,vertex.py,vertex.pz,1),transform));lo.x=std::min(lo.x,q.x);lo.y=std::min(lo.y,q.y);lo.z=std::min(lo.z,q.z);hi.x=std::max(hi.x,q.x);hi.y=std::max(hi.y,q.y);hi.z=std::max(hi.z,q.z);}
    atmosphere.fires.push_back({{lo.x,lo.y,lo.z,0},{hi.x,hi.y,hi.z,0},{p.color.x,p.color.y,p.color.z,1}});continue;
   }

   if(!externalFogObstacles && p.fogSolid && atmosphere.obstacleLow.size()<128){
    XMFLOAT3 lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};
    for(int k=0;k<8;++k){XMFLOAT3 q;XMStoreFloat3(&q,XMVector3TransformCoord(XMVectorSet(k&1?p.fogHigh.x:p.fogLow.x,k&2?p.fogHigh.y:p.fogLow.y,k&4?p.fogHigh.z:p.fogLow.z,1),transform));lo.x=std::min(lo.x,q.x);lo.y=std::min(lo.y,q.y);lo.z=std::min(lo.z,q.z);hi.x=std::max(hi.x,q.x);hi.y=std::max(hi.y,q.y);hi.z=std::max(hi.z,q.z);}
    atmosphere.obstacleLow.emplace_back(lo.x,lo.y,lo.z,0);atmosphere.obstacleHigh.emplace_back(hi.x,hi.y,hi.z,0);
   }
   if(rt.active>0&&!p.source->verts.empty()&&p.source->verts[0].kind==9)transform=transform*XMMatrixTranslation(.012f*sinf(time*24),0,.012f*cosf(time*31));
   auto color=p.color;
   if(p.effect==1){float pulse=1+.12f*sinf(time*9+p.x)+.05f*sinf(time*17+p.z);transform=XMMatrixScaling(1,pulse,1)*transform;color.y*=.85f+.15f*sinf(time*7);}
   if(p.effect==2){float pulse=.88f+.12f*sinf(time*1.7f);color.x*=pulse;color.y*=pulse;}
   // Deformed GPU grass writes its own motion history; its 2200 CPU vertex identities per patch (hundreds of thousands per frame) are never read.
   const bool gpuGrassPart=!d.editorRaytracing&&!p.source->verts.empty()&&GrassDeformation::accepts(p.source->verts.front().kind);
   if(rt.active>0||d.editorRaytracing){XMFLOAT4X4 world;XMStoreFloat4x4(&world,transform);rtItems.push_back({p.meshKey,p.source.get(),world,{color.x,color.y,color.z,p.emissive},p.motionKey});}
   if(gpuGrassPart&&!p.vertexIdentities.empty()){
    // Rest positions never change, so the per-frame history rewrite (hundreds of thousands of hash inserts) is done once per part.
    auto& recorded=staticRecorded[p.vertexIdentities.front()];
    uint64_t fingerprint=p.source->verts.size();{const size_t n=p.source->verts.size();for(size_t k:{size_t(0),n/3,n/2,n-1}){const auto& q=p.source->verts[k];uint32_t bits[3];memcpy(bits,&q.px,4);memcpy(bits+1,&q.py,4);memcpy(bits+2,&q.pz,4);for(uint32_t b:bits)fingerprint=(fingerprint^b)*1099511628211ull;}}
    if(recorded.first!=p.source.get()||recorded.second!=fingerprint){recorded={p.source.get(),fingerprint};
     for(size_t i=0;i<p.vertexIdentities.size();++i)if(auto id=p.vertexIdentities[i]){const auto& point=p.source->verts[i];staticVertices.set(id,{point.px,point.py,point.pz});}}
   }
   else for(size_t i=0;i<p.vertexIdentities.size();++i)if(auto id=p.vertexIdentities[i]){if(i+16<p.vertexIdentities.size())currentVertices.prefetch(p.vertexIdentities[i+16]);const auto& point=p.source->verts[i];currentVertices.set(id,{point.px,point.py,point.pz});}
   const float kind=p.source->verts.empty()?0:p.source->verts[0].kind;
   const GpuMesh* rendered=&p.gpu;
   if(!d.editorRaytracing&&GrassDeformation::accepts(kind))rendered=&grass.draw(d,grassKey,transform,time);
   if(kind>6.8&&kind<7.5)liquids.push_back({&p.gpu,transform,color,p.emissive,p.motionKey});else{
        static const int fragDiag=[]{char value[8]{};return GetEnvironmentVariableA("STARLIGHT_DIAG_FRAGMENTS",value,8)?atoi(value):0;}();const bool fragment=(p.meshKey>>62)==3; /* diagnostic: 1 hides fragments from the scene pass, 2 from the shadow pass, 3 both */
    if(!(fragment&&(fragDiag&1))){XMFLOAT3 middle{(p.shadowLow.x+p.shadowHigh.x)*.5f,(p.shadowLow.y+p.shadowHigh.y)*.5f,(p.shadowLow.z+p.shadowHigh.z)*.5f};XMFLOAT3 center;XMStoreFloat3(&center,XMVector3TransformCoord(XMLoadFloat3(&middle),transform));const float dx=center.x-d.camWorld.x,dy=center.y-d.camWorld.y,dz=center.z-d.camWorld.z;solids.push_back({rendered,transform,color,p.emissive,p.motionKey,dx*dx+dy*dy+dz*dz});}
    if((kind<6.8f&&p.emissive<=0)||(kind>9.5f&&kind<11.5f)){
     auto low=p.shadowLow,high=p.shadowHigh;
     if(kind>.5f&&kind<1.2f){float h=std::max(0.f,high.y);low.x-=h*h*.155f;high.x+=h*h*.155f;low.z-=h*h*.095f;high.z+=h*h*.095f;}
     if(GrassDeformation::accepts(kind)){float reach=grass.reach(grassKey,transform);XMFLOAT3 padding;XMStoreFloat3(&padding,XMVectorSet(reach/std::max(.001f,XMVectorGetX(XMVector3Length(transform.r[0]))),reach/std::max(.001f,XMVectorGetX(XMVector3Length(transform.r[1]))),reach/std::max(.001f,XMVectorGetX(XMVector3Length(transform.r[2]))),0));low.x-=padding.x;low.y-=padding.y;low.z-=padding.z;high.x+=padding.x;high.y+=padding.y;high.z+=padding.z;}
     if(!(fragment&&(fragDiag&2)))casters.push_back({rendered,transform,low,high});
    }
   }
  }
  const auto segT1=DrawClock::now();grass.end(d,time);d.gpuMark(5);const auto segT2=DrawClock::now();sunShadows.draw(d,casters,lights,time);d.gpuMark(6);const auto segT3=DrawClock::now();pointShadows.draw(d,sunShadows,casters,lights,time);d.gpuMark(1);const auto segT4=DrawClock::now();surfaces.bind(d);d.cmd->SetPipelineState(d.pso.Get());d.writeFrameCB(lights);frameLights=lights;
    {static const bool sortSolids=[]{char value[8]{};return !(GetEnvironmentVariableA("STARLIGHT_SORT_SOLIDS",value,8)&&value[0]=='0');}();
   // Opaque geometry nearest-first lets the depth test reject hidden pixels before their (expensive) shading; order does not change the image.
   if(sortSolids)std::stable_sort(solids.begin(),solids.end(),[](const LiquidDraw& a,const LiquidDraw& b){return a.distance<b.distance;});}
  {static const bool prepass=[]{char value[8]{};return !(GetEnvironmentVariableA("STARLIGHT_ZPREPASS",value,8)&&value[0]=='0');}(); /* depth pre-pass of the opaque draws: later passes shade only the surviving samples */
   if(prepass&&!d.editorRaytracing&&solids.size()>1){
    std::vector<UINT> staged(solids.size());
    for(size_t i=0;i<solids.size();++i){d.editorMotionKey=solids[i].key;staged[i]=d.stageMesh(solids[i].world,solids[i].color,solids[i].emissive);d.editorMotionKey=0;}
    d.cmd->SetPipelineState(d.psoDepthOnly.Get());for(size_t i=0;i<solids.size();++i)d.issueMesh(staged[i],*solids[i].mesh);
    d.gpuMark(7);
    d.cmd->SetPipelineState(d.psoDepthEqual.Get());for(size_t i=0;i<solids.size();++i)d.issueMesh(staged[i],*solids[i].mesh);
    d.gpuMark(8);
    d.cmd->SetPipelineState(d.pso.Get());
   }else for(const auto& solid:solids){d.editorMotionKey=solid.key;d.drawMesh(*solid.mesh,solid.world,solid.color,solid.emissive);d.editorMotionKey=0;}}
  size_t telegraphIndex=0;for(const auto& effect:telegraphs){uint64_t identity=telegraphIndex<telegraphIdentities.size()?telegraphIdentities[telegraphIndex]:0;++telegraphIndex;
   const int rings=std::max(1,int(ceilf(effect.range/.5f)));
   auto found=std::find_if(telegraphTemplates.begin(),telegraphTemplates.end(),[&](const auto& t){return t.rings==rings && t.halfAngle==effect.halfAngle;});
   if(found==telegraphTemplates.end())continue;
   const float radius=effect.range*effect.progress,yaw=atan2f(-effect.aimX,-effect.aimZ);
   auto transform=XMMatrixScaling(radius,1,radius)*XMMatrixRotationY(yaw)*XMMatrixTranslation(effect.x,effect.y,effect.z);
   d.editorMotionKey=identity;d.drawMesh(found->gpu,transform,{effect.r,effect.g,effect.b},1);d.editorMotionKey=0;
  }
  d.gpuMark(9);
  if(!liquids.empty()&&!d.editorRaytracing)surfaces.capture(d);
  for(const auto& liquid:liquids){d.editorMotionKey=liquid.key;d.drawMesh(*liquid.mesh,liquid.world,liquid.color,liquid.emissive);d.editorMotionKey=0;}
  d.gpuMark(10);
  {const auto segT5=DrawClock::now();auto ms=[](DrawClock::time_point a,DrawClock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();};drawSegments[0]=ms(segT0,segT1);drawSegments[1]=ms(segT1,segT2);drawSegments[2]=ms(segT2,segT3);drawSegments[3]=ms(segT3,segT4);drawSegments[4]=ms(segT4,segT5);}
  previousVertices.swap(currentVertices);geometryFresh=false;
 }
};
static thread_local std::string failure;
#define API extern "C" __declspec(dllexport)
API int sl_grass_contacts(void* handle,const float* boxes,int count){
 try{if(!handle||count<0||count>int(kMaxInstances)+1||(count&&!boxes))throw std::runtime_error("Invalid grass contacts");
  std::vector<GrassDeformation::Body> contacts;contacts.reserve(count);
  for(int i=0;i<count;++i){const float* p=boxes+i*6;for(int j=0;j<6;++j)if(!std::isfinite(p[j]))throw std::runtime_error("Nonfinite grass contact");if(p[0]>p[3]||p[1]>p[4]||p[2]>p[5])throw std::runtime_error("Inverted grass contact");contacts.push_back({{p[0],p[1],p[2],0},{p[3],p[4],p[5],0}});}
  auto& v=*static_cast<Viewport*>(handle);v.grass.bodies(*v.renderer,contacts);return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_grass_stats(void* handle,uint64_t* values){
 if(!handle||!values)return 0;auto& g=static_cast<Viewport*>(handle)->grass;values[0]=g.entries.size();values[1]=g.contacts.size();values[2]=g.dispatches;values[3]=0;
 for(const auto& pair:g.entries)values[3]+=pair.second.allocationBytes;
 values[3]+=g.poolBytes();
 for(const auto& pair:g.topologies)if(auto topology=pair.second.lock())values[3]+=topology->allocationBytes;
 values[3]+=g.bodyAllocationBytes;return 1;
}
API int sl_grass_readback(void* handle,int instance,float* output,int capacity){
 try{if(!handle||instance<0||capacity<0)throw std::runtime_error("Invalid grass readback");auto& v=*static_cast<Viewport*>(handle);auto& d=*v.renderer;
  auto it=v.grass.entries.begin();for(int i=0;i<instance&&it!=v.grass.entries.end();++i)++it;if(it==v.grass.entries.end())throw std::runtime_error("Grass instance unavailable");auto& entry=it->second;
  int count=int(entry.topology->vertices);if(!output)return count;if(capacity<count||!entry.readable)throw std::runtime_error("Grass readback requires a rendered instance and sufficient capacity");
  d.flush();auto readback=GrassDeformation::buffer(d,size_t(count)*sizeof(Vertex),D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_COPY_DEST);
  d.alloc->Reset();d.cmd->Reset(d.alloc.Get(),nullptr);d.barrier(entry.mesh.vb.Get(),D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,D3D12_RESOURCE_STATE_COPY_SOURCE);d.cmd->CopyBufferRegion(readback.Get(),0,entry.mesh.vb.Get(),UINT64(entry.offset)*sizeof(Vertex),size_t(count)*sizeof(Vertex));d.barrier(entry.mesh.vb.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);d.cmd->Close();ID3D12CommandList* lists[]={d.cmd.Get()};d.queue->ExecuteCommandLists(1,lists);d.flush();
  void* mapped=nullptr;D3D12_RANGE range{0,size_t(count)*sizeof(Vertex)};d.must(readback->Map(0,&range,&mapped),"grass readback map");memcpy(output,mapped,range.End);D3D12_RANGE none{0,0};readback->Unmap(0,&none);return count;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_lightning(float x,float y,float z,float spread,float* output,int capacity){try{if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(spread)||spread<0||spread>5)return 0;auto mesh=starlight::Lightning({x,y,z},spread);if(output){if(capacity<(int)mesh.size())return 0;memcpy(output,mesh.data(),mesh.size()*sizeof(starlight::ArcVertex));}return (int)mesh.size();}catch(...){return 0;}}
API int sl_motion_readback(void* handle,double* output){
 try{if(!handle||!output)throw std::runtime_error("Invalid motion readback");auto& viewport=*static_cast<Viewport*>(handle);auto& d=*viewport.renderer;auto motion=viewport.rt.active>0?viewport.rt.images[25].Get():d.sceneMvec.Get();if(!motion)throw std::runtime_error("Motion texture unavailable");d.flush();auto desc=starlight_resource_desc(motion);D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows;UINT64 rowBytes,total;d.device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&total);
 D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> readback;d.must(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"Motion readback allocation");
 d.alloc->Reset();d.cmd->Reset(d.alloc.Get(),nullptr);d.barrier(motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION destination{};destination.pResource=readback.Get();destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;destination.PlacedFootprint=footprint;D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=motion;source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;d.cmd->CopyTextureRegion(&destination,0,0,0,&source,nullptr);d.barrier(motion,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);d.cmd->Close();ID3D12CommandList* lists[]={d.cmd.Get()};d.queue->ExecuteCommandLists(1,lists);d.flush();
 uint8_t* mapped=nullptr;D3D12_RANGE range{0,size_t(total)};d.must(readback->Map(0,&range,reinterpret_cast<void**>(&mapped)),"Map motion readback");
 auto half=[](uint16_t value){int exponent=(value>>10)&31,mantissa=value&1023;double result=exponent==0?std::ldexp(double(mantissa),-24):exponent==31?INFINITY:std::ldexp(double(mantissa+1024),exponent-25);return value&32768?-result:result;};
 double peak=0,sum=0,moving=0;for(UINT y=0;y<rows;++y){auto* row=reinterpret_cast<const uint16_t*>(mapped+footprint.Offset+size_t(y)*footprint.Footprint.RowPitch);for(UINT x=0;x<desc.Width;++x){double magnitude=std::hypot(half(row[x*2]),half(row[x*2+1]));if(!std::isfinite(magnitude)){readback->Unmap(0,nullptr);throw std::runtime_error("Nonfinite rendered motion vector");}peak=std::max(peak,magnitude);sum+=magnitude;moving+=magnitude>.001;}}
 D3D12_RANGE written{0,0};readback->Unmap(0,&written);output[0]=peak;output[1]=sum/(double(desc.Width)*rows);output[2]=moving;return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
static int fog_readback(void* handle,double* output,double* channels=nullptr){
 try{if(!handle||!output)throw std::runtime_error("Invalid fog readback");auto& v=*static_cast<Viewport*>(handle);auto& d=*v.renderer;auto field=v.atmosphere.fogField[v.atmosphere.fogIndex];if(!field)throw std::runtime_error("Fog field is not initialized");d.flush();auto desc=starlight_resource_desc(field);D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows;UINT64 rowBytes,total;d.device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&total);
 D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> readback;d.must(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"Fog readback allocation");
 d.alloc->Reset();d.cmd->Reset(d.alloc.Get(),nullptr);d.barrier(field.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION destination{};destination.pResource=readback.Get();destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;destination.PlacedFootprint=footprint;D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=field.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;d.cmd->CopyTextureRegion(&destination,0,0,0,&source,nullptr);d.barrier(field.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);d.cmd->Close();ID3D12CommandList* lists[]={d.cmd.Get()};d.queue->ExecuteCommandLists(1,lists);d.flush();
 uint8_t* mapped=nullptr;D3D12_RANGE range{0,size_t(total)};d.must(readback->Map(0,&range,reinterpret_cast<void**>(&mapped)),"Map fog readback");
 auto half=[](uint16_t value){int exponent=(value>>10)&31,mantissa=value&1023;double result=exponent==0?std::ldexp(double(mantissa),-24):exponent==31?INFINITY:std::ldexp(double(mantissa+1024),exponent-25);return value&32768?-result:result;};
 double mass=0,dark=0,cx=0,cy=0,cz=0,solid=0,speed=0,mistMass=0,sootMass=0,chaosMass=0,chaosX=0,chaosY=0,chaosZ=0;
 for(UINT z=0;z<footprint.Footprint.Depth;++z)for(UINT y=0;y<rows;++y){auto* row=reinterpret_cast<const uint16_t*>(mapped+footprint.Offset+(size_t(z)*rows+y)*footprint.Footprint.RowPitch);for(UINT x=0;x<desc.Width;++x){double mist=half(row[x*4]),smoke=half(row[x*4+1])+half(row[x*4+2]),vx=0,vz=half(row[x*4+3]);if(!std::isfinite(mist+smoke+vx+vz)){readback->Unmap(0,nullptr);throw std::runtime_error("Nonfinite fog field");}double px=-16+(x+.5)*.5,py=-2+(y+.5)*.5,pz=-16+(z+.5)*.5;double soot=half(row[x*4+1]),chaos=half(row[x*4+2]);mistMass+=mist;sootMass+=soot;chaosMass+=chaos;chaosX+=chaos*px;chaosY+=chaos*py;chaosZ+=chaos*pz;mass+=mist+smoke;dark+=smoke;cx+=smoke*px;cy+=smoke*py;cz+=smoke*pz;speed=std::max(speed,std::hypot(vx,vz));bool buried=py<=v.atmosphere.terrainHeights[z*64+x];if(buried)solid+=mist+smoke;for(size_t i=0;i<v.atmosphere.obstacleLow.size();++i){auto lo=v.atmosphere.obstacleLow[i],hi=v.atmosphere.obstacleHigh[i];if(!buried&&px>=lo.x&&px<=hi.x&&py>=lo.y&&py<=hi.y&&pz>=lo.z&&pz<=hi.z){solid+=mist+smoke;break;}}}}

 D3D12_RANGE written{0,0};readback->Unmap(0,&written);output[0]=mass;output[1]=dark;output[2]=dark>0?cx/dark:0;output[3]=dark>0?cy/dark:0;output[4]=dark>0?cz/dark:0;output[5]=solid;output[6]=speed;if(channels){channels[0]=mistMass;channels[1]=sootMass;channels[2]=chaosMass;channels[3]=solid;channels[4]=chaosMass>0?chaosX/chaosMass:0;channels[5]=chaosMass>0?chaosY/chaosMass:0;channels[6]=chaosMass>0?chaosZ/chaosMass:0;channels[7]=speed;}return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_fog_readback(void* handle,double* output){return fog_readback(handle,output);}
API int sl_fog_channels(void* handle,double* output){if(!output)return 0;double aggregate[7]{};return fog_readback(handle,aggregate,output);}
API int sl_environment_lights(void* handle,const float* data,int count){
 try{if(!handle||count<0||count>32||(count&&!data))throw std::runtime_error("Invalid environment lights");std::vector<SpotLight> lights;for(int i=0;i<count;++i){auto p=data+i*12;for(int j=0;j<12;++j)if(!std::isfinite(p[j]))throw std::runtime_error("Nonfinite environment light");lights.push_back({{p[0],p[1],p[2],p[3]},{p[4],p[5],p[6],p[7]},{p[8],p[9],p[10],p[11]}});}static_cast<Viewport*>(handle)->localLights=std::move(lights);return 1;}catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_fog_sources(void* handle,const float* data,int count){
 try{if(!handle||count<0||count>16||(count&&!data))throw std::runtime_error("Invalid smoke sources");std::vector<XMFLOAT4> sources;for(int i=0;i<count;++i){auto p=data+i*4;for(int j=0;j<4;++j)if(!std::isfinite(p[j]))throw std::runtime_error("Nonfinite smoke source");if(p[3]<=0)throw std::runtime_error("Invalid smoke radius");sources.emplace_back(p[0],p[1],p[2],p[3]);}static_cast<Viewport*>(handle)->smoke=std::move(sources);return 1;}catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_fog_source_types(void* handle,const float* data,int count){
 try{if(!handle||count<0||count>16||(count&&!data))throw std::runtime_error("Invalid fog source types");auto& v=*static_cast<Viewport*>(handle);if(size_t(count)!=v.smoke.size())throw std::runtime_error("Fog source type count mismatch");std::vector<float> types;for(int i=0;i<count;++i){if(data[i]!=0&&data[i]!=1)throw std::runtime_error("Invalid fog source type");types.push_back(data[i]);}v.atmosphere.sourceTypes=std::move(types);return 1;}catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_fog_terrain(void* handle,const float* heights){
 try{if(!handle||!heights)throw std::runtime_error("Invalid fog terrain");auto& a=static_cast<Viewport*>(handle)->atmosphere;for(int i=0;i<4096;++i){if(!std::isfinite(heights[i]))throw std::runtime_error("Nonfinite fog terrain");a.terrainHeights[i]=heights[i];}return 1;}catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_fog_obstacles(void* handle,const float* data,int count){
 try{if(!handle||count<0||count>128||(count&&!data))throw std::runtime_error("Invalid fog obstacles");auto& v=*static_cast<Viewport*>(handle);std::vector<XMFLOAT4> low,high,velocity;
 for(int i=0;i<count;++i){const auto* p=data+i*9;for(int j=0;j<9;++j)if(!std::isfinite(p[j]))throw std::runtime_error("Nonfinite fog obstacle");low.emplace_back(p[0],p[1],p[2],0);high.emplace_back(p[3],p[4],p[5],0);velocity.emplace_back(p[6],p[7],p[8],0);}
 v.externalFogObstacles=true;v.atmosphere.obstacleLow=std::move(low);v.atmosphere.obstacleHigh=std::move(high);v.atmosphere.obstacleVelocity=std::move(velocity);return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
API void sl_environment_reset(void* handle){auto& v=*static_cast<Viewport*>(handle);v.grass.reset();v.weather={};v.weatherLow={};v.weatherHigh={};v.atmosphere.reset();v.rt.previousWorldByKey.clear();v.hasEffectTime=false;v.renderer->prevModelByKey.clear();v.renderer->curModelByKey.clear();v.renderer->resetHistory=true;v.previousVertices.clear();v.currentVertices.clear();}
API int sl_surface_weather(void* handle,float moisture,float rain,float windX,float windZ,const float* boxes,int count){
 try{
  if(!handle||!std::isfinite(moisture)||moisture<0||moisture>1||!std::isfinite(rain)||rain<0||rain>1||!std::isfinite(windX)||!std::isfinite(windZ)||std::abs(windX)>20||std::abs(windZ)>20||count<0||count>64||(count&&!boxes))throw std::runtime_error("Invalid surface weather");
  std::array<XMFLOAT4,64> low{},high{};
  for(int i=0;i<count;++i){const float* b=boxes+i*6;for(int k=0;k<6;++k)if(!std::isfinite(b[k]))throw std::runtime_error("Nonfinite weather shelter");if(b[0]>b[3]||b[1]>b[4]||b[2]>b[5])throw std::runtime_error("Invalid weather shelter bounds");low[i]={b[0],b[1],b[2],1};high[i]={b[3],b[4],b[5],1};}
  auto& v=*static_cast<Viewport*>(handle);v.weather={moisture,rain,windX,windZ};v.weatherLow=low;v.weatherHigh=high;return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_effect_clock(void* handle,double seconds){if(!handle||!std::isfinite(seconds)||seconds<0||seconds>FLT_MAX)return 0;auto& v=*static_cast<Viewport*>(handle);v.externalEffectClock=true;v.effectClock=float(seconds);return 1;}
API float sl_effect_time(void* handle){return static_cast<Viewport*>(handle)->previousEffectTime;}
API const char* sl_error(){return failure.c_str();}
API int sl_debug_fail_hresult(int n){const int remaining=RenderDevice::hrFaultCountdown;RenderDevice::hrFaultCountdown=n<0?0:n;return remaining;}
// Optional ABI: old DLLs can be tested without importing unavailable symbols.
API void sl_surface_texture_stats(void* handle,uint64_t* output){auto& maps=static_cast<Viewport*>(handle)->surfaces;output[0]=maps.resolution;output[1]=maps.levels;output[2]=maps.residentBytes;output[3]=SurfaceTextures::count;}
API uint64_t sl_water_scene_bytes(void* handle){return static_cast<Viewport*>(handle)->surfaces.waterSceneBytes;}
API int sl_surface_texture_tail(void* handle,uint8_t* output){try{if(!handle||!output)throw std::runtime_error("Invalid surface readback");auto& v=*static_cast<Viewport*>(handle);v.surfaces.tail(*v.renderer,output);return 1;}catch(const std::exception& e){failure=e.what();return 0;}}
API int sl_local_shadow_stats(void* handle,uint64_t* stats){if(!handle||!stats)return 0;auto& s=static_cast<Viewport*>(handle)->pointShadows;stats[0]=s.resolution;stats[1]=s.bytes;stats[2]=s.capacity;stats[3]=s.draws;stats[4]=s.culled;stats[5]=s.enabled?1:0;return 1;}
API int sl_shadow_settings(void* handle,int quality){try{if(!handle||quality<0||quality>3)throw std::runtime_error("Shadow quality must be Off, Low, Medium or High");auto& v=*static_cast<Viewport*>(handle);if(v.shadowQuality==quality)return 1;v.sunShadows.setQuality(*v.renderer,v.surfaces.heap.Get(),quality);v.pointShadows.setQuality(*v.renderer,v.surfaces.heap.Get(),quality);v.shadowQuality=quality;v.renderer->resetHistory=true;return 1;}catch(const std::exception& e){failure=e.what();return 0;}}
API int sl_sun_shadow_stats(void* handle,uint64_t* stats){if(!handle||!stats)return 0;auto& s=static_cast<Viewport*>(handle)->sunShadows;stats[0]=s.resolution;stats[1]=s.bytes;stats[2]=s.draws;stats[3]=s.enabled?1:0;return 1;}
API int sl_benchmark_contract(){return 2;} // owned offscreen target; no swapchain writes
API void sl_benchmark(void* handle,int enable){auto& d=*static_cast<Viewport*>(handle)->renderer;d.setAuditOffscreen(enable!=0);}
API void sl_benchmark_flush(void* handle){static_cast<Viewport*>(handle)->renderer->flush();}
API void sl_scene_resources(void* handle,unsigned long long* parts,unsigned long long* blas,unsigned long long* uploads){auto& v=*static_cast<Viewport*>(handle);*parts=v.parts.size();*blas=v.rt.cache.size();*uploads=v.renderer->uploadKeepAlive.size();}
API void sl_upload_pool_stats(void* handle,unsigned long long* count,unsigned long long* bytes,unsigned long long* created,unsigned long long* reused){auto& d=*static_cast<Viewport*>(handle)->renderer;*count=d.uploadPool.size();*bytes=d.uploadPoolBytes;*created=d.uploadCreated;*reused=d.uploadReused;}
API int sl_lifetime_stats(void* handle,uint64_t* values,int count){
 if(!handle||!values||count<16)return 0;auto& v=*static_cast<Viewport*>(handle);
 const auto& rt=v.rt.retired.stats;const auto& gr=v.grass.retired.stats;
 values[0]=rt.pending;values[1]=rt.drained;values[2]=rt.maxPending;values[3]=rt.forcedStalls+v.rt.buildRing.stalls;
 values[4]=gr.pending+(v.grass.staged.items?1:0);values[5]=gr.drained;values[6]=gr.maxPending;values[7]=gr.forcedStalls;
 values[8]=v.retiredResident.size();values[9]=v.residentDrained;values[10]=v.residentMaxPending;values[11]=v.residentForcedStalls;
 values[12]=rt.pendingBytes;values[13]=gr.pendingBytes;values[14]=rt.forcedStallMicros+gr.forcedStallMicros+v.residentForcedMicros;values[15]=v.sideStaging.size();
 return 1;
}
API void* sl_create(HWND window,int width,int height){
 try{
  wchar_t debugLog[32768];DWORD debugLen=GetEnvironmentVariableW(L"STARLIGHT_NATIVE_LOG",debugLog,32768);if(debugLen>0&&debugLen<32768&&!g_logFile)_wfopen_s(&g_logFile,debugLog,L"w");
  if(!starlight::valid_render_size(width,height))throw std::runtime_error(starlight::renderSizeError);
  auto v=std::make_unique<Viewport>();v->window=window;v->renderer=std::make_unique<RenderDevice>();auto& d=*v->renderer;
  d.editorKeyedHistory=true;d.d3dDebug=GetEnvironmentVariableW(L"STARLIGHT_D3D_DEBUG",nullptr,0)>0;d.linearHdrScene=true;d.frameRateLimit=500;d.vsync=false;d.upsRequest="off";d.modeRequest="native";d.fgRequest=false;d.cullOn=false;d.editorCameraDistance=13;
  Mesh cube;BuildBox(cube);d.init(window,width,height,cube);wchar_t reflections[8]{};if(GetEnvironmentVariableW(L"STARLIGHT_SCREEN_REFLECTIONS",reflections,8)){if(wcscmp(reflections,L"0")&&wcscmp(reflections,L"1"))throw std::runtime_error("Screen reflections must be 0 or 1");v->screenReflections=reflections[0]==L'1';}v->surfaces.init(d);v->sunShadows.init(d,v->surfaces.heap.Get());v->pointShadows.init(d,v->surfaces.heap.Get());v->rt.surfaces=&v->surfaces;v->post.init(d);v->atmosphere.init(d,v->post.scene.Get());v->rt.init(d);v->prepareTelegraph(.785398163f,6);v->prepareTelegraph(float(.2617993877991494),40);auto raw=v.get();d.editorDraw=[raw]{raw->draw();};d.editorPost=[raw]{float time=raw->visualTime();bool animated=!raw->freeze&&(raw->cycle>0||raw->fogDensity>0||!raw->smoke.empty());for(auto& p:raw->parts)if(p.effect||(!p.source->verts.empty()&&p.source->verts[0].kind>=7))animated|=!raw->freeze;auto input=raw->rt.draw(*raw->renderer,raw->rtItems,raw->frameLights,time,raw->fogDensity,raw->smoke,animated);raw->atmosphere.draw(*raw->renderer,raw->frameLights,time,raw->fogDensity,raw->smoke,input,raw->rt.active>=3&&raw->rt.tlasBuilt?raw->rt.tlas->GetGPUVirtualAddress():0,raw->rt.active);raw->post.draw(*raw->renderer);};return v.release();
 }catch(const std::exception& e){failure=e.what();return nullptr;}
}
API int sl_resize(void* handle,int width,int height){
 try{
  if(!handle)throw std::runtime_error("Invalid viewport handle");
  if(!starlight::valid_render_size(width,height))throw std::runtime_error(starlight::renderSizeError);
  auto& v=*static_cast<Viewport*>(handle);auto& d=*v.renderer;
  if(!v.resizeStale&&d.w==(UINT)width&&d.h==(UINT)height)return 1;
  v.resizeStale=true;d.resizeNativeOutput(width,height,true);v.surfaces.resize(d);v.rt.resize(d);v.post.resize(d);v.atmosphere.linearOutput=v.post.scene;v.atmosphere.resize(d);v.resizeStale=false;return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
static_assert(offsetof(Vertex,previousX)==10*sizeof(float));
static void attach_history(Vertex& vertex,const VertexHistory::Slot* previous){if(previous&&memcmp(&previous->x,&vertex.px,3*sizeof(float))!=0){vertex.previousX=previous->x;vertex.previousY=previous->y;vertex.previousZ=previous->z;vertex.hasPrevious=1;}}
static uint64_t mesh_geometry_hash(const Mesh& mesh){uint64_t hash=1469598103934665603ull;auto mix=[&](const void* bytes,size_t count){auto ptr=(const uint8_t*)bytes;for(size_t j=0;j<count;j++){hash^=ptr[j];hash*=1099511628211ull;}};for(const auto& vertex:mesh.verts)mix(&vertex,offsetof(Vertex,previousX));mix(mesh.indices.data(),mesh.indices.size()*sizeof(uint32_t));return hash;}
static uint64_t mesh_history_hash(const Mesh& mesh){uint64_t hash=1469598103934665603ull;bool any=false;auto mix=[&](const void* bytes,size_t count){auto ptr=(const uint8_t*)bytes;for(size_t j=0;j<count;j++){hash^=ptr[j];hash*=1099511628211ull;}};for(size_t i=0;i<mesh.verts.size();++i){const auto& vertex=mesh.verts[i];if(vertex.hasPrevious>0){const uint64_t index=i;mix(&index,sizeof(index));mix(&vertex.previousX,4*sizeof(float));any=true;}}return any?hash|1:0;}
static uint64_t mesh_identity(const Mesh& mesh){const uint64_t geometry=mesh_geometry_hash(mesh),history=mesh_history_hash(mesh);return (history?geometry^(history+0x9e3779b97f4a7c15ull+(geometry<<6)+(geometry>>2)):geometry)&0x7fffffffffffffffull;}
using SceneClock=std::chrono::steady_clock;
static double sceneMs(SceneClock::time_point a,SceneClock::time_point b){return std::chrono::duration<double,std::milli>(b-a).count();}
// Scene parts, resident instances and telegraphs each take one constant-buffer draw slot per frame, and the background fill takes one more; draws past kMaxInstances would be dropped silently.
static void require_draw_slots(size_t sceneParts,size_t instances,size_t telegraphs){if(sceneParts+instances+telegraphs>=kMaxInstances)throw std::runtime_error("Scene parts, instances and telegraphs exceed native draw capacity");}
static int install_scene(void* handle,const uint8_t* bytes,int length,bool delta,uint64_t deltaToken){
 try{
  if(!handle||!bytes||length<8)throw std::runtime_error("Missing viewport scene");
  auto& v=*static_cast<Viewport*>(handle);auto& d=*v.renderer;
  const auto sceneBegin=SceneClock::now();const uint64_t uploadsBefore=reinterpret_cast<Viewport*>(handle)->uploads;auto lap=sceneBegin;double hashMs=0,boundsMs=0;uint64_t vertexTotal=0;
  Reader r{bytes,(size_t)length};auto version=r.get<uint32_t>();if(delta){if(version!=0x43564c53)throw std::runtime_error("Unsupported viewport delta version");auto token=r.get<uint64_t>();if(!token||token!=v.baseToken||token!=deltaToken||v.baseCount>v.parts.size())throw std::runtime_error("Viewport delta does not match the resident base scene");version=0x42564c53;}else if(version!=0x31564c53&&version!=0x32564c53&&version!=0x33564c53&&version!=0x34564c53&&version!=0x35564c53&&version!=0x36564c53&&version!=0x37564c53&&version!=0x38564c53&&version!=0x39564c53&&version!=0x41564c53&&version!=0x42564c53)throw std::runtime_error("Unsupported viewport scene version");
  auto count=r.get<uint32_t>();require_draw_slots((delta?v.baseCount:0)+size_t(count),0,0);
  std::vector<SpotLight> localLights;if(!delta&&version>=0x32564c53){auto lightCount=r.get<uint32_t>();if(lightCount>32)throw std::runtime_error("Too many lights");localLights=r.array<SpotLight>(lightCount);}
  float hour=15,cycle=0,dayLength=180,density=.018f;std::vector<XMFLOAT4> smoke;std::vector<float> sourceTypes;if(!delta&&version>=0x35564c53){hour=r.get<float>();cycle=r.get<float>();dayLength=r.get<float>();density=r.get<float>();if(!std::isfinite(hour)||!std::isfinite(cycle)||!std::isfinite(dayLength)||!std::isfinite(density)||hour<0||hour>24||dayLength<10||density<0||density>.3f)throw std::runtime_error("Invalid atmosphere");auto n=r.get<uint32_t>();if(n>16)throw std::runtime_error("Too many smoke volumes");for(uint32_t i=0;i<n;++i){auto source=r.get<XMFLOAT4>();float type=version>=0x42564c53?r.get<float>():1.f;if(!std::isfinite(source.x)||!std::isfinite(source.y)||!std::isfinite(source.z)||!std::isfinite(source.w)||source.w<=0||!std::isfinite(type)||(type!=0&&type!=1))throw std::runtime_error("Invalid fog emitter");smoke.push_back(source);sourceTypes.push_back(type);}}
  int rtTier=delta?int(v.rt.requested):0,rtBounces=3,rtFreeze=0;if(!delta&&version>=0x36564c53){rtTier=r.get<int>();rtBounces=r.get<int>();rtFreeze=r.get<int>();if(rtTier<0||rtTier>4||rtBounces<2||rtBounces>4||rtFreeze<0||rtFreeze>1)throw std::runtime_error("Invalid raytracing settings");}
  int rrMode=1;if(!delta&&version>=0x37564c53){rrMode=r.get<int>();if(rrMode<0||rrMode>1)throw std::runtime_error("Invalid denoiser setting");}
  struct Incoming {Part part;Mesh mesh;};std::vector<Incoming> incoming;
  for(uint32_t i=0;i<count;i++){
   Incoming in;if(version>=0x39564c53)in.part.motionKey=r.get<uint64_t>();if(version>=0x38564c53){auto transient=r.get<uint32_t>();if(transient>1)throw std::runtime_error("Invalid transient geometry flag");in.part.streaming=transient && rtTier==0;}if(version>=0x32564c53){in.part.emissive=r.get<float>();in.part.effect=r.get<int>();in.part.baseY=r.get<float>();}in.part.color=r.get<XMFLOAT3>();in.part.x=r.get<float>();in.part.z=r.get<float>();in.part.size=r.get<float>();in.part.speed=r.get<float>();
   in.part.route=r.array<XMFLOAT2>(r.get<uint32_t>());if(version>=0x33564c53)in.part.local=r.get<XMFLOAT4X4>();auto vertices=r.get<uint32_t>();if(version>=0x34564c53){struct PriorVertex{float px,py,pz,nx,ny,nz,r,g,b,kind;};if(size_t(vertices)*(sizeof(PriorVertex)+(version>=0x41564c53?sizeof(uint64_t):0))<=r.left){in.part.vertexIdentities.reserve(vertices);in.mesh.verts.reserve(vertices);}for(uint32_t i=0;i<vertices;++i){auto p=r.get<PriorVertex>();Vertex vertex{p.px,p.py,p.pz,p.nx,p.ny,p.nz,p.r,p.g,p.b,p.kind};uint64_t id=version>=0x41564c53?r.get<uint64_t>():0;in.part.vertexIdentities.push_back(id);if(version>=0x41564c53&&i+16<vertices&&r.left>=size_t(16)*(sizeof(PriorVertex)+sizeof(uint64_t))){uint64_t ahead;memcpy(&ahead,r.p+size_t(15)*(sizeof(PriorVertex)+sizeof(uint64_t))+sizeof(PriorVertex),sizeof(ahead));if(ahead)v.previousVertices.prefetch(ahead);}if(id){auto previous=v.previousVertices.find(id);if(!previous)previous=v.staticVertices.find(id);attach_history(vertex,previous);}in.mesh.verts.push_back(vertex);}}else{struct OldVertex{float x,y,z,nx,ny,nz;};for(auto p:r.array<OldVertex>(vertices))in.mesh.verts.push_back({p.x,p.y,p.z,p.nx,p.ny,p.nz});}in.mesh.indices=r.array<uint32_t>(r.get<uint32_t>());
   auto finite=[](const float* p,size_t count){for(size_t k=0;k<count;k++)if(!std::isfinite(p[k]))return false;return true;};
   if(!finite(&in.part.color.x,3)||!finite(&in.part.local._11,16)||!std::isfinite(in.part.x)||!std::isfinite(in.part.z)||!std::isfinite(in.part.size)||!std::isfinite(in.part.speed)||!std::isfinite(in.part.emissive)||!std::isfinite(in.part.baseY)||in.part.size<=0)throw std::runtime_error("Invalid mesh transform/material");
   for(auto p:in.part.route)if(!std::isfinite(p.x)||!std::isfinite(p.y))throw std::runtime_error("Invalid route coordinate");
   for(const auto& vertex:in.mesh.verts)if(!finite(&vertex.px,sizeof(Vertex)/sizeof(float)))throw std::runtime_error("Non-finite mesh vertex");
   if(in.mesh.indices.size()%3)throw std::runtime_error("Incomplete mesh triangle");
   if(in.mesh.verts.empty()||in.mesh.indices.empty())throw std::runtime_error("Empty mesh");
   for(auto index:in.mesh.indices)if(index>=in.mesh.verts.size())throw std::runtime_error("Invalid mesh index");
   if(in.part.streaming){
    // Transient meshes already have a fenced upload/reuse path. Their changing
    // vertices do not need an expensive content hash or immutable-cache lookup.
    if(v.transientIdentity==0x7fffffffffffffffull)throw std::runtime_error("Transient geometry identity exhausted");
    in.part.meshKey=0x8000000000000000ull|++v.transientIdentity;
   }else{
    const auto hashBegin=SceneClock::now();
    in.part.meshKey=mesh_identity(in.mesh);hashMs+=sceneMs(hashBegin,SceneClock::now());
   }
   float kind=in.mesh.verts.front().kind;
   in.part.fogSolid=in.part.speed>0 && in.part.emissive==0 && (kind==0 || kind==6 || kind==10 || kind==-40);
   if(in.part.fogSolid){in.part.fogLow={1e20f,1e20f,1e20f};in.part.fogHigh={-1e20f,-1e20f,-1e20f};for(const auto& q:in.mesh.verts){auto& lo=in.part.fogLow;auto& hi=in.part.fogHigh;lo.x=std::min(lo.x,q.px);lo.y=std::min(lo.y,q.py);lo.z=std::min(lo.z,q.pz);hi.x=std::max(hi.x,q.px);hi.y=std::max(hi.y,q.py);hi.z=std::max(hi.z,q.pz);}}
   vertexTotal+=in.mesh.verts.size();incoming.push_back(std::move(in));
  }
  if(r.left)throw std::runtime_error("Unexpected viewport scene data");
  {std::set<uint64_t> identities;const size_t first=delta?v.baseCount:0; // rejected before anything is replaced, so a failed scene leaves the resident base and its token intact
   for(size_t i=0;i<first;++i)if(GrassDeformation::accepts(v.parts[i].source->verts.front().kind))identities.insert(GrassDeformation::identity(v.parts[i].motionKey,i));
   for(size_t i=0;i<incoming.size();++i)if(GrassDeformation::accepts(incoming[i].mesh.verts.front().kind)&&!identities.insert(GrassDeformation::identity(incoming[i].part.motionKey,first+i)).second)throw std::runtime_error("Duplicate grass instance identity");}
  const auto parsed=SceneClock::now();const double parseMs=sceneMs(lap,parsed)-hashMs;lap=parsed;
  std::unordered_map<uint64_t,std::shared_ptr<Mesh>> sources;for(auto& p:v.parts)sources.emplace(p.meshKey,p.source);
  // Bounds are cached once at scene upload, never reconstructed per light/frame.
  // Conservative wind padding keeps shader-deformed vegetation inside them.
  for(auto& in:incoming){XMFLOAT3 lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};for(const auto& v:in.mesh.verts){lo.x=std::min(lo.x,v.px);lo.y=std::min(lo.y,v.py);lo.z=std::min(lo.z,v.pz);hi.x=std::max(hi.x,v.px);hi.y=std::max(hi.y,v.py);hi.z=std::max(hi.z,v.pz);}in.part.shadowLow=lo;in.part.shadowHigh=hi;}
  {const auto now=SceneClock::now();boundsMs=sceneMs(lap,now);lap=now;}
  std::unordered_map<uint64_t,GpuMesh> cache;for(auto& p:v.parts)cache.emplace(p.meshKey,p.gpu);
  bool changed=false;for(auto& in:incoming)if(!in.part.streaming && !cache.count(in.part.meshKey))changed=true;
  {size_t streamingNow=0;for(const auto& in:incoming)if(in.part.streaming)++streamingNow;v.streamDemandPerInstall=std::max(v.streamDemandPerInstall,streamingNow);}
  const bool rasterOnly=v.rt.active==0&&!v.rt.tlasBuilt&&v.rt.cache.empty();Viewport::SideScope side(v);
  double flushMs=0;if(changed)side.begin();
  std::vector<Part> next;for(auto& in:incoming){auto found=cache.find(in.part.meshKey);if(found==cache.end()){in.part.gpu=in.part.streaming?v.streamMesh(in.mesh):d.uploadMesh(in.mesh);v.uploads++;cache.emplace(in.part.meshKey,in.part.gpu);}else in.part.gpu=found->second;auto source=sources.find(in.part.meshKey);if(source!=sources.end())in.part.source=source->second;else{in.part.source=std::make_shared<Mesh>(std::move(in.mesh));sources.emplace(in.part.meshKey,in.part.source);}next.push_back(std::move(in.part));}
  if(changed)side.submit();
  // Retire removed buffers only after their previous frame has completed.
  std::unordered_set<uint64_t> nextKeys;nextKeys.reserve(next.size()+(delta?v.baseCount:0));for(const auto& p:next)nextKeys.insert(p.meshKey);if(delta)for(size_t i=0;i<v.baseCount;++i)nextKeys.insert(v.parts[i].meshKey);
  bool removed=false;for(const auto& old:v.parts)if(!old.streaming && !nextKeys.count(old.meshKey))removed=true;
  if(removed){if(!rasterOnly)v.rt.retireGeometry(d);v.rtItems.clear();}
  for(auto& old:cache)if(!nextKeys.count(old.first)){
   auto original=std::find_if(v.parts.begin(),v.parts.end(),[&](const Part& p){return p.meshKey==old.first;});
   if(original!=v.parts.end() && original->streaming)v.streams.push_back({old.second,d.fenceVal,starlight_resource_desc(old.second.vb).Width,starlight_resource_desc(old.second.ib).Width});
   else v.retiredResident.push_back({old.second,d.fenceVal});
  }
  if(delta){v.parts.erase(v.parts.begin()+v.baseCount,v.parts.end());for(auto& p:next)v.parts.push_back(std::move(p));}else{v.parts=std::move(next);v.baseCount=0;v.baseToken=0;}
  {const auto now=SceneClock::now();v.sceneStats.upload+=sceneMs(lap,now);lap=now;}
  std::set<uint64_t> grassKeys;bool grassChanged=false;
  for(size_t i=0;i<v.parts.size();++i){const auto& p=v.parts[i];if(!GrassDeformation::accepts(p.source->verts.front().kind))continue;auto key=GrassDeformation::identity(p.motionKey,i);if(!grassKeys.insert(key).second)throw std::runtime_error("Duplicate grass instance identity");auto old=v.grass.entries.find(key);grassChanged|=old==v.grass.entries.end()||old->second.meshKey!=p.meshKey;}
  bool grassRemoved=false;for(const auto& entry:v.grass.entries)grassRemoved|=!grassKeys.count(entry.first);
  if(grassChanged||grassRemoved){if(grassChanged){if(!v.grass.bodyRing)v.grass.bodies(d,{});Viewport::SideScope grassSide(v);grassSide.begin();for(size_t i=0;i<v.parts.size();++i){const auto& p=v.parts[i];if(GrassDeformation::accepts(p.source->verts.front().kind))v.grass.prepare(d,GrassDeformation::identity(p.motionKey,i),p.meshKey,*p.source,p.gpu);}grassSide.submit();}v.grass.retain(d,grassKeys);}
  v.geometryFresh=true;if(!delta)v.warmStreams();
  if(!delta){v.localLights=std::move(localLights);v.hour=hour;v.cycle=cycle;v.dayLength=dayLength;v.fogDensity=density;v.smoke=std::move(smoke);v.atmosphere.sourceTypes=std::move(sourceTypes);v.rt.denoiserMode=rrMode;v.rt.requested=rtTier;v.rt.bounces=rtBounces;if(rtFreeze&&!v.freeze)v.frozenTime=v.visualTime();v.freeze=rtFreeze!=0;if(version<0x39564c53)d.resetHistory=true;v.baseCount=0;v.baseToken=0;}
  {auto& st=v.sceneStats;const auto end=SceneClock::now();if(starlight_diag::events_enabled()){starlight_diag::evlog("scn",sceneMs(sceneBegin,end),parseMs+hashMs+boundsMs,double(v.uploads-uploadsBefore));starlight_diag::evlog("scn2",changed?1.0:0.0,delta?1.0:0.0,double(incoming.size()));starlight_diag::evlog("scn3",flushMs,v.streamMs,v.streamCreateMs);starlight_diag::evlog("scn4",double(v.streamCreated),double(v.streamReused),double(v.streams.size()));{size_t biggest=0;for(const auto& in:incoming)biggest=std::max(biggest,in.mesh.verts.size());starlight_diag::evlog("scn5",double(vertexTotal),double(biggest),double(length));}v.streamMs=0;v.streamCreateMs=0;}st.parse+=parseMs;st.hash+=hashMs;st.bounds+=boundsMs;st.tail+=sceneMs(lap,end);st.total+=sceneMs(sceneBegin,end);++st.calls;if(delta)++st.deltaCalls;st.bytes+=uint64_t(length);st.vertices+=vertexTotal;}
  return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_scene(void* handle,const uint8_t* bytes,int length){return install_scene(handle,bytes,length,false,0);}
// Installs a complete packet and keeps its parts as the resident static base identified by token.
API int sl_scene_base(void* handle,const uint8_t* bytes,int length,uint64_t token){
 if(!token){failure="Resident scene token must be nonzero";return 0;}
 if(!install_scene(handle,bytes,length,false,0))return 0;
 auto& v=*static_cast<Viewport*>(handle);v.baseCount=v.parts.size();v.baseToken=token;return 1;
}
// Replaces only the parts after the resident base. The token must match the installed base.
API int sl_scene_delta(void* handle,const uint8_t* bytes,int length,uint64_t token){return install_scene(handle,bytes,length,true,token);}
API int sl_scene_stats(void* handle,double* output,int capacity){
 if(!handle||!output||capacity<10)return 0;auto& st=static_cast<Viewport*>(handle)->sceneStats;
 const double values[10]={st.parse,st.hash,st.bounds,st.upload,st.tail,st.total,double(st.calls),double(st.deltaCalls),double(st.bytes),double(st.vertices)};
 for(int i=0;i<10;++i)output[i]=values[i];return 10;
}
// ---- resident rigid meshes -------------------------------------------------------------------------------------
API int sl_resident_mesh(void* handle,const uint8_t* bytes,int length){
 // Batch: u32 count, then count x (u32 size, mesh blob). One command list and one GPU wait for the whole batch.
 try{
  if(!handle||!bytes||length<4)throw std::runtime_error("Missing resident mesh batch");
  auto& v=*static_cast<Viewport*>(handle);auto& d=*v.renderer;
  Reader batch{bytes,(size_t)length};
  const auto meshCount=batch.get<uint32_t>();
  struct Incoming{uint64_t key;std::shared_ptr<Mesh> mesh;XMFLOAT3 lo,hi;};
  std::vector<Incoming> incoming;
  for(uint32_t m=0;m<meshCount;++m){
   const auto size=batch.get<uint32_t>();if(size>batch.left)throw std::runtime_error("Incomplete resident mesh batch");
   Reader r{batch.p,size};batch.p+=size;batch.left-=size;
   if(r.get<uint32_t>()!=0x44564c53)throw std::runtime_error("Unsupported resident mesh version");
   const auto key=r.get<uint64_t>();if(!key)throw std::runtime_error("Resident mesh key must be nonzero");
   if(v.residentMeshes.contains(key))throw std::runtime_error("Resident mesh key already installed");
   for(const auto& other:incoming)if(other.key==key)throw std::runtime_error("Duplicate resident mesh key");
   auto mesh=std::make_shared<Mesh>();
   const auto vertices=r.get<uint32_t>();
   struct PriorVertex{float px,py,pz,nx,ny,nz,r,g,b,kind;};
   if(size_t(vertices)*(sizeof(PriorVertex)+sizeof(uint64_t))>r.left)throw std::runtime_error("Incomplete resident mesh");
   mesh->verts.reserve(vertices);
   XMFLOAT3 lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};
   for(uint32_t i=0;i<vertices;++i){
    auto p=r.get<PriorVertex>();r.get<uint64_t>();
    const float* f=&p.px;for(int k=0;k<10;++k)if(!std::isfinite(f[k]))throw std::runtime_error("Non-finite resident vertex");
    mesh->verts.push_back(Vertex{p.px,p.py,p.pz,p.nx,p.ny,p.nz,p.r,p.g,p.b,p.kind});
    lo.x=std::min(lo.x,p.px);lo.y=std::min(lo.y,p.py);lo.z=std::min(lo.z,p.pz);hi.x=std::max(hi.x,p.px);hi.y=std::max(hi.y,p.py);hi.z=std::max(hi.z,p.pz);
   }
   mesh->indices=r.array<uint32_t>(r.get<uint32_t>());
   if(r.left)throw std::runtime_error("Unexpected resident mesh data");
   if(mesh->verts.empty()||mesh->indices.empty()||mesh->indices.size()%3)throw std::runtime_error("Invalid resident mesh");
   for(auto index:mesh->indices)if(index>=mesh->verts.size())throw std::runtime_error("Invalid resident mesh index");
   incoming.push_back({key,std::move(mesh),lo,hi});
  }
  if(batch.left)throw std::runtime_error("Unexpected resident mesh batch data");
  if(incoming.empty())return 1;
  const double um0=starlight_diag::events_enabled()?starlight_diag::evnow():0;Viewport::SideScope side(v);side.begin();const double um1=um0>0?starlight_diag::evnow():0;
  std::vector<GpuMesh> uploaded;uploaded.reserve(incoming.size());
  for(const auto& in:incoming)uploaded.push_back(d.uploadMesh(*in.mesh));
  side.submit();const double um2=um0>0?starlight_diag::evnow():0;if(um0>0)starlight_diag::evlog("rmesh",double(incoming.size()),um1-um0,starlight_diag::evnow()-um2);
  for(size_t i=0;i<incoming.size();++i)v.residentMeshes.emplace(incoming[i].key,Viewport::ResidentMesh{incoming[i].mesh,std::move(uploaded[i]),incoming[i].lo,incoming[i].hi});
  return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
static void recycle_retired_resident(Viewport& v,bool all){
 const double started=starlight_diag::events_enabled()?starlight_diag::evnow():0;size_t recycledMeshes=0;
 auto& d=*v.renderer;const UINT64 completed=d.fence->GetCompletedValue();
 const size_t minimum=all?SIZE_MAX:std::max<size_t>(16,v.retiredResident.size()/8);
 const auto begin=std::chrono::steady_clock::now();size_t finished=0;
 while(finished<v.retiredResident.size()&&(all||v.retiredResident[finished].fence<=completed)){
  if(!all&&finished>=minimum&&std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count()>.35)break;
  auto& item=v.retiredResident[finished];d.recycleBuffer(item.gpu.vb);d.recycleBuffer(item.gpu.ib);++finished;
 }
 recycledMeshes=finished;v.residentDrained+=finished;v.residentMaxPending=std::max<uint64_t>(v.residentMaxPending,v.retiredResident.size());v.retiredResident.erase(v.retiredResident.begin(),v.retiredResident.begin()+finished);
 while(v.retiredResident.size()>Viewport::kRetiredResidentCap){
  const UINT64 done=d.fence->GetCompletedValue();size_t freed=0;
  std::erase_if(v.retiredResident,[&](Viewport::RetiredResident& item){if(item.fence>done)return false;d.recycleBuffer(item.gpu.vb);d.recycleBuffer(item.gpu.ib);++freed;return true;});
  v.residentDrained+=freed;recycledMeshes+=freed;if(v.retiredResident.size()<=Viewport::kRetiredResidentCap)break;
  UINT64 oldest=UINT64_MAX;for(const auto& item:v.retiredResident)oldest=std::min(oldest,item.fence);
  const double waitStart=starlight_diag::evnow();d.waitFence(oldest);const double waited=starlight_diag::evnow()-waitStart;
  ++v.residentForcedStalls;v.residentForcedMicros+=uint64_t(waited>0?waited*1000.:0.);starlight_diag::evlog("ltRes",double(v.retiredResident.size()),-1.,waited);
 }
 v.grass.drain(d,all);v.reapSideStaging();
 for(size_t freed=0;freed<4&&!v.sourceGraveyard.empty();++freed)v.sourceGraveyard.pop_back();
 if(started>0)starlight_diag::evlog("recycle",starlight_diag::evnow()-started,double(recycledMeshes),double(v.retiredResident.size()));if(started>0)starlight_diag::evlog("pool",double(d.bufferPool.size()),double(d.bufferPoolBytes)/1048576.0,double(d.bufferEvicted));
}
// Returns once the graphics card has finished every command submitted so far (signals the GPU fence and waits on it). Render thread only.
API int sl_wait_gpu(void* handle){
 try{if(!handle)throw std::runtime_error("Missing viewport");auto& v=*static_cast<Viewport*>(handle);v.renderer->flush();v.rt.drainRetired(*v.renderer,true);recycle_retired_resident(v,true);return 1;}
 catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_resident_release(void* handle,const uint64_t* keys,int count){
 try{
  if(!handle||count<0||(count&&!keys))throw std::runtime_error("Invalid resident release");
  auto& v=*static_cast<Viewport*>(handle);auto& d=*v.renderer;
  const double relStart=starlight_diag::events_enabled()?starlight_diag::evnow():0;std::unordered_set<uint64_t> gone;gone.reserve(size_t(count));
  for(int i=0;i<count;++i){
   auto found=v.residentMeshes.find(keys[i]);if(found==v.residentMeshes.end())continue;
   const uint64_t meshKey=0xC000000000000000ull|keys[i];
   gone.insert(meshKey);
   // In-flight frames may still read these buffers: recycle once the GPU has passed the current fence value.
   v.retiredResident.push_back({std::move(found->second.gpu),d.fenceVal});if(found->second.source)v.sourceGraveyard.push_back(std::move(found->second.source));v.residentMeshes.erase(found);
  }
  const double relMid=relStart>0?starlight_diag::evnow():0;if(!gone.empty())std::erase_if(v.instanceParts,[&](const Part& p){return gone.count(p.meshKey)!=0;});const double relErased=relStart>0?starlight_diag::evnow():0;
  recycle_retired_resident(v,false);if(relStart>0)starlight_diag::evlog("rel",relMid-relStart,relErased-relMid,double(count));
  return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
// Replaces the whole instance list: one 64-byte record per instance.
struct InstanceWire{uint64_t mesh,motion;float px,py,pz,qx,qy,qz,qw,r,g,b,emissive,reserved;};
static_assert(sizeof(InstanceWire)==64);
API int sl_instances(void* handle,const uint8_t* bytes,int count){
 try{
  if(!handle||count<0||(count&&!bytes))throw std::runtime_error("Invalid instance list");
  auto& v=*static_cast<Viewport*>(handle);
  require_draw_slots(v.parts.size(),size_t(count),v.telegraphs.size());
  std::vector<Part> next;next.reserve(count);
  for(int i=0;i<count;++i){
   InstanceWire w;memcpy(&w,bytes+size_t(i)*sizeof(InstanceWire),sizeof(w));
   const float* f=&w.px;for(int k=0;k<12;++k)if(!std::isfinite(f[k]))throw std::runtime_error("Non-finite instance");
   auto found=v.residentMeshes.find(w.mesh);if(found==v.residentMeshes.end())throw std::runtime_error("Instance references an unknown resident mesh");
   const auto& resident=found->second;
   Part part;
   part.source=resident.source;part.gpu=resident.gpu;part.meshKey=0xC000000000000000ull|w.mesh;part.motionKey=w.motion;part.streaming=false;
   part.shadowLow=resident.low;part.shadowHigh=resident.high;
   part.color={w.r,w.g,w.b};part.x=0;part.z=0;part.size=1;part.speed=0;part.emissive=w.emissive;part.baseY=0;part.effect=0;
   XMStoreFloat4x4(&part.local,XMMatrixRotationQuaternion(XMVectorSet(w.qx,w.qy,w.qz,w.qw))*XMMatrixTranslation(w.px,w.py,w.pz));
   next.push_back(std::move(part));
  }
    {const double s0=starlight_diag::events_enabled()?starlight_diag::evnow():0;v.instanceParts=std::move(next);const double s1=s0>0?starlight_diag::evnow():0;recycle_retired_resident(v,false);if(s0>0)starlight_diag::evlog("inst",s1-s0,double(count),starlight_diag::evnow()-s1);}
  return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
// Removes base parts (by motion key) from the resident static scene without rebuilding or re-parsing the rest.
API int sl_scene_remove(void* handle,const uint64_t* keys,int count){
 try{
  if(!handle||count<0||(count&&!keys))throw std::runtime_error("Invalid scene removal");
  auto& v=*static_cast<Viewport*>(handle);auto& d=*v.renderer;
  if(!v.baseToken)throw std::runtime_error("No resident base scene");
  const double rmStart=starlight_diag::events_enabled()?starlight_diag::evnow():0;std::unordered_set<uint64_t> wanted(keys,keys+count);
  std::vector<Part> removed;size_t write=0;
  for(size_t i=0;i<v.baseCount;++i){
   if(v.parts[i].motionKey&&wanted.count(v.parts[i].motionKey))removed.push_back(std::move(v.parts[i]));
   else{if(write!=i)v.parts[write]=std::move(v.parts[i]);++write;}
  }
  if(removed.empty())return 1;
  const size_t tail=v.parts.size()-v.baseCount;
  for(size_t k=0;k<tail;++k)v.parts[write+k]=std::move(v.parts[v.baseCount+k]);
  v.parts.resize(write+tail);v.baseCount=write;
  std::unordered_set<uint64_t> live;for(const auto& p:v.parts)live.insert(p.meshKey);
  bool retire=false,grassRemoved=false;
  for(const auto& p:removed){if(!p.streaming&&!live.count(p.meshKey))retire=true;if(p.source&&!p.source->verts.empty()&&GrassDeformation::accepts(p.source->verts.front().kind))grassRemoved=true;}
  // Transient (streaming) meshes are not pooled with the resident ones: in-flight frames may still read them, so they go to the spare list behind the current fence.
  for(auto& p:removed)if(p.streaming&&p.gpu.vb&&p.gpu.ib){const UINT64 vbWidth=starlight_resource_desc(p.gpu.vb).Width,ibWidth=starlight_resource_desc(p.gpu.ib).Width;v.streams.push_back({std::move(p.gpu),d.fenceVal,vbWidth,ibWidth});}
  const bool rasterOnly=v.rt.active==0&&!v.rt.tlasBuilt&&v.rt.cache.empty();
  const int removeBranch=(retire||grassRemoved)?(rasterOnly?0:1):2;
  if(retire||grassRemoved){
   std::unordered_set<uint64_t> deferred;
   for(auto& p:removed)if(!p.streaming&&!live.count(p.meshKey)&&deferred.insert(p.meshKey).second)v.retiredResident.push_back({std::move(p.gpu),d.fenceVal});
   if(grassRemoved){
    std::set<uint64_t> grassKeys;
    for(size_t i=0;i<v.parts.size();++i){const auto& p=v.parts[i];if(GrassDeformation::accepts(p.source->verts.front().kind))grassKeys.insert(GrassDeformation::identity(p.motionKey,i));}
    v.grass.retain(d,grassKeys);
   }
   if(retire&&!rasterOnly)v.rt.retireGeometry(d);
   v.rtItems.clear();recycle_retired_resident(v,false);
  }
  v.geometryFresh=true;if(rmStart>0)starlight_diag::evlog("srem",starlight_diag::evnow()-rmStart,double(removeBranch),double(removed.size()));
  return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_telegraphs(void* handle,const float* data,int count){
 try{
  if(!handle || count<0 || count>4096 || (count && !data))throw std::runtime_error("Invalid GPU telegraph instances");
  auto& v=*static_cast<Viewport*>(handle);require_draw_slots(v.parts.size(),v.instanceParts.size(),size_t(count));std::vector<Viewport::TelegraphInstance> next(count);
  static_assert(sizeof(Viewport::TelegraphInstance)==11*sizeof(float));
  if(count)memcpy(next.data(),data,size_t(count)*sizeof(Viewport::TelegraphInstance));
  for(const auto& e:next)for(int i=0;i<11;++i)if(!std::isfinite(reinterpret_cast<const float*>(&e)[i]))throw std::runtime_error("Non-finite GPU telegraph");
  std::vector<std::pair<float,int>> required;
  for(const auto& e:next){auto key=std::make_pair(e.halfAngle,std::max(1,int(ceilf(std::clamp(e.range,0.f,32.f)/.5f))));if(std::find(required.begin(),required.end(),key)==required.end())required.push_back(key);}
  if(required.size()>64)throw std::runtime_error("Too many distinct GPU effect templates");
  size_t missing=0;for(auto key:required)if(std::none_of(v.telegraphTemplates.begin(),v.telegraphTemplates.end(),[&](const auto& t){return t.halfAngle==key.first && t.rings==key.second;}))++missing;
  if(v.telegraphTemplates.size()+missing>64){for(auto& evicted:v.telegraphTemplates)v.retiredResident.push_back({std::move(evicted.gpu),v.renderer->fenceVal});v.telegraphTemplates.clear();v.telegraphBytes=0;recycle_retired_resident(v,false);}
  for(const auto& e:next){for(int i=0;i<11;++i)if(!std::isfinite(reinterpret_cast<const float*>(&e)[i]))throw std::runtime_error("Non-finite GPU telegraph");
   if(e.range<=0 || e.range>32 || e.progress<0 || e.progress>1 || e.halfAngle<=0 || e.halfAngle>3.1415928f || e.aimX*e.aimX+e.aimZ*e.aimZ<.25f)throw std::runtime_error("Invalid GPU telegraph parameters");
   v.prepareTelegraph(e.halfAngle,std::max(1,int(ceilf(e.range/.5f))));
  }
  v.telegraphs=std::move(next);return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_telegraph_ids(void* handle,const uint64_t* ids,int count){if(!handle||count<0||(count&&!ids))return 0;auto& v=*static_cast<Viewport*>(handle);if(size_t(count)!=v.telegraphs.size())return 0;v.telegraphIdentities.clear();if(count)v.telegraphIdentities.assign(ids,ids+count);return 1;}
API void sl_telegraph_stats(void* handle,unsigned long long* count,unsigned long long* bytes){auto& v=*static_cast<Viewport*>(handle);*count=v.telegraphs.size();*bytes=v.telegraphBytes;}
API int sl_frame(void* handle,double seconds,float distance){
 try{if(!handle||!std::isfinite(seconds)||!std::isfinite(distance))throw std::runtime_error("Invalid frame input");auto& v=*static_cast<Viewport*>(handle);recycle_retired_resident(v,false);v.seconds=seconds;v.renderer->editorTime=v.visualTime();v.renderer->editorCameraDistance=distance;std::vector<DxrItem> geometry;for(auto& p:v.parts)if(p.effect!=4)geometry.push_back({p.meshKey,p.source.get(),{}, {}});for(auto& p:v.instanceParts)geometry.push_back({p.meshKey,p.source.get(),{}, {}});try{v.rt.prepare(*v.renderer,geometry);}catch(const std::exception& ex){v.rt.active=0;v.rt.supported=false;v.rt.status=std::string("DXR initialization failed; raster fallback: ")+ex.what();}v.renderer->editorRaytracing=v.rt.active>0;v.pointShadows.prepare(*v.renderer,v.surfaces.heap.Get(),v.rt.active>0?0:PointShadows::count(v.localLights));{static FILE* stall=[]()->FILE*{char path[512];if(!GetEnvironmentVariableA("STARLIGHT_STALL_LOG",path,512))return nullptr;strcat_s(path,".viewport");FILE* file=nullptr;fopen_s(&file,path,"w");return file;}();const auto renderBegin=std::chrono::steady_clock::now();v.renderer->renderEmpty(v.window);if(starlight_diag::events_enabled()){auto& m=v.renderer->stageMarks;starlight_diag::evlog("vf",1000*(m[9]-m[0]),1000*(m[4]-m[3]),1000*(m[5]-m[4]));starlight_diag::evlog("vf2",1000*(m[7]-m[6]),1000*(m[8]-m[7]),1000*(m[9]-m[8]));starlight_diag::evlog("vf3",v.drawSegments[0],v.drawSegments[1],v.drawSegments[4]);starlight_diag::evlog("vf4",1000*(m[1]-m[0]),1000*(m[2]-m[1]),1000*(m[3]-m[2]));starlight_diag::evlog("vf5",1000*(m[6]-m[5]),v.drawSegments[2],v.drawSegments[3]);}{static FILE* passLog=[]()->FILE*{char path[512];if(!GetEnvironmentVariableA("STARLIGHT_PASS_LOG",path,512))return nullptr;FILE* file=nullptr;fopen_s(&file,path,"w");return file;}();if(passLog){static uint64_t lines=0;fprintf(passLog,"%.3f %.3f %.3f %.3f %.3f\n",std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count(),v.renderer->lastPassMs[0],v.renderer->lastPassMs[1],v.renderer->lastPassMs[2],v.renderer->lastPassMs[3]);if((++lines&1023)==0)fflush(passLog);}}if(stall){const double total=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-renderBegin).count();if(total>12){fprintf(stall,"sl_frame render %.1f ms: classify %.1f grass.end %.1f sunShadows %.1f pointShadows %.1f solids+liquids %.1f | latencyWait %.1f pace %.1f slotFence+reset %.1f scenePass %.1f evalPost %.1f submit %.1f present %.1f\n",total,v.drawSegments[0],v.drawSegments[1],v.drawSegments[2],v.drawSegments[3],v.drawSegments[4],1000*(v.renderer->stageMarks[1]-v.renderer->stageMarks[0]),1000*(v.renderer->stageMarks[2]-v.renderer->stageMarks[1]),1000*(v.renderer->stageMarks[3]-v.renderer->stageMarks[2]),1000*(v.renderer->stageMarks[4]-v.renderer->stageMarks[3]),1000*(v.renderer->stageMarks[5]-v.renderer->stageMarks[4]),1000*(v.renderer->stageMarks[6]-v.renderer->stageMarks[5]),1000*(v.renderer->stageMarks[7]-v.renderer->stageMarks[6]));fflush(stall);}}}if(FAILED(v.renderer->device->GetDeviceRemovedReason())){v.renderer->reportDeviceLoss();throw std::runtime_error("Native graphics device lost");}return 1;}
 catch(const std::exception& e){failure=e.what();return 0;}
}
// Read on the render owner thread; no cross-DLL containers or borrowed pointers.
API void sl_multicore_begin(void* handle,int enabled){auto& p=static_cast<Viewport*>(handle)->renderer->parallelMetrics;p.enabled=enabled!=0;p.reset();}
API int sl_multicore_read(void* handle,double* output,int capacity){
 auto& r=static_cast<Viewport*>(handle)->renderer->parallelMetrics.reading;
 int required=6+int(r.chunkMs.size());if(capacity<required)return required;
 output[0]=r.parallelBatches;output[1]=r.serialBatches;output[2]=r.participants;output[3]=double(r.items);output[4]=r.wallMs;output[5]=r.callerWaitMs;
 for(size_t i=0;i<r.chunkMs.size();++i)output[6+i]=r.chunkMs[i];return required;
}
API void sl_wait_timings(void* handle,double* fence,double* pacing,double* present){auto& d=*static_cast<Viewport*>(handle)->renderer;*fence=d.measuredFenceWaitMs;*pacing=d.measuredPacingWaitMs;*present=d.measuredPresentWaitMs;}
API double sl_gpu_ms(void* handle){return static_cast<Viewport*>(handle)->renderer->lastGpuMs;}
API int sl_gpu_memory(void* handle,uint64_t* output){
 if(!handle||!output)return 0;auto& d=*static_cast<Viewport*>(handle)->renderer;ComPtr<IDXGIAdapter3> adapter;DXGI_QUERY_VIDEO_MEMORY_INFO info{};
 if(FAILED(d.adapter.As(&adapter))||FAILED(adapter->QueryVideoMemoryInfo(0,DXGI_MEMORY_SEGMENT_GROUP_LOCAL,&info)))return 0;
 output[0]=info.CurrentUsage;output[1]=info.Budget;return 1;
}
API int sl_hdr_post_stats(void* handle,uint64_t* output){
 if(!handle||!output)return 0;const auto& post=static_cast<Viewport*>(handle)->post;
 output[0]=post.enabled?post.levels:0;output[1]=post.allocationBytes;output[2]=post.enabled?1:0;output[3]=post.passes.size();return 1;
}
API int sl_hdr_post_settings(void* handle,int quality,float strength){
 try{
  if(!handle||quality<0||quality>3||!std::isfinite(strength)||strength<0||strength>.5f)throw std::runtime_error("Invalid glow settings");
  auto& v=*static_cast<Viewport*>(handle);auto& post=v.post;const UINT levels=quality==3?6:quality==1?3:5;bool enabled=quality>0;
  if(v.postStale||post.enabled!=enabled||post.levels!=levels){v.postStale=true;v.renderer->flush();post.enabled=enabled;post.levels=levels;post.resize(*v.renderer);v.atmosphere.linearOutput=post.scene;v.atmosphere.resize(*v.renderer);v.postStale=false;}
  post.strength=strength;return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_render_width(void* handle){return static_cast<Viewport*>(handle)->renderer->rw;}
API int sl_render_height(void* handle){return static_cast<Viewport*>(handle)->renderer->rh;}
API int sl_debug_active(void* handle){return static_cast<Viewport*>(handle)->renderer->d3dq ? 1 : 0;}
API int sl_native_contract(void* handle){auto& d=*static_cast<Viewport*>(handle)->renderer;return d.rw==d.w&&d.rh==d.h&&d.ups->path()==up::Path::Off&&!d.ups->fgEnabled();}
API int sl_output_width(void* handle){return static_cast<Viewport*>(handle)->renderer->w;}
API void sl_camera(void* handle,float x,float y,float z,float tx,float ty,float tz){auto& d=*static_cast<Viewport*>(handle)->renderer;d.editorEye=XMFLOAT3(x,y,z);d.editorTarget=XMFLOAT3(tx,ty,tz);}
API void sl_camera_up(void* handle,float x,float y,float z){auto& d=*static_cast<Viewport*>(handle)->renderer;d.editorUp=XMFLOAT3(x,y,z);}
API uint64_t sl_uploads(void* handle){return static_cast<Viewport*>(handle)->uploads;}
API int sl_parts(void* handle){return (int)static_cast<Viewport*>(handle)->parts.size();}
API int sl_captured(void* handle){return static_cast<Viewport*>(handle)->renderer->shotDone?1:0;}
API void sl_shot(void* handle,const wchar_t* path){auto& d=*static_cast<Viewport*>(handle)->renderer;d.shotPath=path;d.shotAtMs=0;d.shotDone=false;if(!g_logFile){auto log=std::wstring(path)+L".log";_wfopen_s(&g_logFile,log.c_str(),L"w");}}
API void sl_destroy(void* handle){if(!handle)return;auto v=static_cast<Viewport*>(handle);std::string first;auto step=[&](auto&& body){try{body();}catch(const std::exception& e){if(first.empty())first=e.what();}catch(...){if(first.empty())first="unknown error during viewport destruction";}};step([&]{v->renderer->flush();});step([&]{v->rt.drainRetired(*v->renderer,true);});step([&]{recycle_retired_resident(*v,true);});step([&]{v->rt.shutdown();});step([&]{v->renderer->teardown();});step([&]{delete v;});if(!first.empty())failure="Viewport cleanup: "+first;}



API void sl_rt(void* handle,int tier,int bounces){auto& rt=static_cast<Viewport*>(handle)->rt;rt.requested=std::clamp(tier,0,4);rt.bounces=std::clamp(bounces,2,4);rt.samples=0;}
API void sl_freeze(void* handle,int freeze){auto& v=*static_cast<Viewport*>(handle);v.freeze=freeze!=0;v.frozenTime=v.externalEffectClock?v.effectClock:0;}
API int sl_rt_supported(void* handle){return static_cast<Viewport*>(handle)->rt.supported;}
API int sl_rt_active(void* handle){return static_cast<Viewport*>(handle)->rt.active;}
API double sl_ray_ms(void* handle){return static_cast<Viewport*>(handle)->rt.rayMs;}
API double sl_as_ms(void* handle){return static_cast<Viewport*>(handle)->rt.asMs;}
API uint64_t sl_blas_builds(void* handle){return static_cast<Viewport*>(handle)->rt.builds;}
API uint64_t sl_tlas_refits(void* handle){return static_cast<Viewport*>(handle)->rt.refits;}
API int sl_rt_samples(void* handle){return static_cast<Viewport*>(handle)->rt.samples;}
API const char* sl_rt_status(void* handle){return static_cast<Viewport*>(handle)->rt.status.c_str();}

API double sl_denoise_ms(void* handle){return static_cast<Viewport*>(handle)->rt.denoiseMs;}

API uint64_t sl_history_resets(void* handle){return static_cast<Viewport*>(handle)->rt.historyResets;}

API int sl_rr_active(void* handle){return static_cast<Viewport*>(handle)->rt.rrActive?1:0;}
API const char* sl_rr_status(void* handle){return static_cast<Viewport*>(handle)->rt.rr.status.c_str();}
// Stable ABI: 0 = native temporal wavelet, 1 = optional native-resolution RR.
// Do not invert this mapping: existing projects and editor controls persist it.
API void sl_denoiser(void* handle,int mode){auto& rt=static_cast<Viewport*>(handle)->rt;rt.denoiserMode=mode==1?1:0;rt.historyValid=false;}
API int sl_denoise_history_index(void* handle){auto& rt=static_cast<Viewport*>(handle)->rt;return 11+(1-rt.historyParity)*4;}
// Explicit offline validation only. Never called by the interactive frame loop.
API int sl_denoise_capture(void* handle,int index,const wchar_t* path){
 try{
  auto& v=*static_cast<Viewport*>(handle);auto& d=*v.renderer;auto& rt=v.rt;
  if(index<0||index>=(int)DxrPass::imageCount||!rt.images[index])return 0;
  d.flush();auto description=starlight_resource_desc(rt.images[index]);D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};UINT rows;UINT64 rowBytes,bytes;
  d.device->GetCopyableFootprints(&description,0,1,0,&layout,&rows,&rowBytes,&bytes);
  auto readback=rt.buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
  d.alloc->Reset();d.cmd->Reset(d.alloc.Get(),nullptr);rt.imageState(d,index,D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=rt.images[index].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=layout;
  d.cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);rt.imageState(d,index,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  d.cmd->Close();ID3D12CommandList* lists[]={d.cmd.Get()};d.queue->ExecuteCommandLists(1,lists);d.flush();
  uint8_t* data=nullptr;D3D12_RANGE range{0,(SIZE_T)bytes};if(FAILED(readback->Map(0,&range,(void**)&data)))return 0;
  std::ofstream file(path,std::ios::binary);uint32_t header[4]={(UINT)description.Width,description.Height,(UINT)description.Format,(UINT)rowBytes};file.write((char*)header,sizeof(header));
  // Driver-mapped readback memory is CPU-readable but may not be suitable for
  // direct filesystem I/O (notably virtual GPU/shared-folder combinations).
  std::vector<char> rowData(static_cast<size_t>(rowBytes));
  UINT writtenRows=0;
  for(;writtenRows<rows && file;writtenRows++){
    memcpy(rowData.data(),data+layout.Offset+writtenRows*layout.Footprint.RowPitch,rowData.size());
    file.write(rowData.data(),static_cast<std::streamsize>(rowData.size()));
  }
  D3D12_RANGE none{0,0};readback->Unmap(0,&none);
  file.flush();
  if(!file){failure="Denoiser capture output write failed after "+std::to_string(writtenRows)+" of "+std::to_string(rows)+" rows";return 0;}
  file.close();
  if(!file){failure="Denoiser capture output could not be closed successfully";return 0;}
  return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
