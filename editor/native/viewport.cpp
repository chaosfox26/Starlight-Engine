#ifdef STARLIGHT_CMAKE_RENDERER
#include <render_device_editor.h>
#else
#include "render_device_editor.h"
#endif
#include "../../deep/engine/atmosphere.h"
#include "../../deep/engine/chaos_lightning.h"
#include <cstring>
#include <fstream>
#include "../../deep/engine/dxr.h"

struct Reader {
 const uint8_t* p; size_t left;
 template<class T>T get(){if(left<sizeof(T))throw std::runtime_error("Incomplete viewport scene");T v;memcpy(&v,p,sizeof(v));p+=sizeof(v);left-=sizeof(v);return v;}
 template<class T>std::vector<T> array(uint32_t n){if(n>left/sizeof(T))throw std::runtime_error("Invalid viewport array");std::vector<T> v(n);memcpy(v.data(),p,n*sizeof(T));p+=n*sizeof(T);left-=n*sizeof(T);return v;}
};
struct Part { std::shared_ptr<Mesh> source;GpuMesh gpu; uint64_t meshKey=0; bool streaming=false; XMFLOAT3 color; float x,z,size,speed; float emissive=0,baseY=0;int effect=0; XMFLOAT4X4 local{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; std::vector<XMFLOAT2> route; };
struct Viewport {
 DxrPass rt;std::vector<DxrItem> rtItems;bool freeze=false;float frozenTime=0;
 float visualTime()const{return freeze?frozenTime:(float)(GetTickCount64()-started)*.001f;}
 AtmospherePass atmosphere;FrameCB frameLights{};float hour=15,cycle=0,dayLength=180,fogDensity=.018f;std::vector<XMFLOAT4> smoke;ULONGLONG started=GetTickCount64();
 std::unique_ptr<RenderDevice> renderer;
 std::vector<Part> parts;std::vector<SpotLight> localLights;std::vector<XMFLOAT4> shadowBounds;

 struct RetiredStream {GpuMesh mesh;UINT64 fence=0;};
 std::vector<RetiredStream> streams;
 GpuMesh streamMesh(const Mesh& mesh){
  auto& d=*renderer;GpuMesh gpu;const UINT64 vb=mesh.verts.size()*sizeof(Vertex),ib=mesh.indices.size()*sizeof(uint32_t);
  const UINT64 completed=d.fence->GetCompletedValue();
  for(auto it=streams.begin();it!=streams.end();++it)if(it->fence<=completed && it->mesh.vb->GetDesc().Width>=vb && it->mesh.ib->GetDesc().Width>=ib){gpu=std::move(it->mesh);streams.erase(it);break;}
  auto buffer=[&](UINT64 bytes){ComPtr<ID3D12Resource> resource;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_UPLOAD;D3D12_RESOURCE_DESC desc{};desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=std::max<UINT64>(256,bytes);desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;d.must(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&resource)),"create transient geometry buffer");return resource;};
  if(!gpu.vb){gpu.vb=buffer(vb);gpu.ib=buffer(ib);}
  auto write=[&](ID3D12Resource* resource,const void* data,size_t bytes){void* mapped=nullptr;D3D12_RANGE read{0,0};d.must(resource->Map(0,&read,&mapped),"map transient geometry");memcpy(mapped,data,bytes);D3D12_RANGE written{0,bytes};resource->Unmap(0,&written);};
  write(gpu.vb.Get(),mesh.verts.data(),size_t(vb));write(gpu.ib.Get(),mesh.indices.data(),size_t(ib));
  gpu.indexCount=UINT(mesh.indices.size());gpu.vbv={gpu.vb->GetGPUVirtualAddress(),UINT(vb),UINT(sizeof(Vertex))};gpu.ibv={gpu.ib->GetGPUVirtualAddress(),UINT(ib),DXGI_FORMAT_R32_UINT};
  // Only completed spare buffers may be evicted. Live draws retain their resources.
  UINT64 retained=0;for(auto it=streams.begin();it!=streams.end();){UINT64 bytes=it->mesh.vb->GetDesc().Width+it->mesh.ib->GetDesc().Width;if(it->fence<=completed && (streams.size()>48 || retained+bytes>32ull*1024*1024))it=streams.erase(it);else{retained+=bytes;++it;}}
  return gpu;
 }
 double seconds=0;uint64_t uploads=0;
 HWND window=nullptr;
 std::string error;
 void draw(){
  auto& d=*renderer;rtItems.clear();
  float time=visualTime();
  float angle=(hour+(cycle>0?time/dayLength*24:0)-6)*3.14159265f/12;float elevation=sinf(angle),day=std::clamp(elevation*2,0.f,1.f);
  FrameCB lights{};lights.lightDir=XMFLOAT4(cosf(angle),-std::max(.03f,elevation),-.35f,day*3.8f);lights.ambient=XMFLOAT4(.025f+day*.355f,.018f+day*.442f,.07f+day*.50f,.16f+day*.12f);lights.camPos=XMFLOAT4(d.camWorld.x,d.camWorld.y,d.camWorld.z,1);lights.fog=XMFLOAT4(.12f,.18f,.24f,fogDensity);
  for(size_t i=0;i<localLights.size()&&i<32;i++){lights.spots[i]=localLights[i];if(lights.spots[i].dir.w<-2.5f){float pulse=.72f+.28f*sinf(time*37+floorf(time*19)*3.1f);lights.spots[i].color.x*=pulse;lights.spots[i].color.y*=pulse;lights.spots[i].color.z*=pulse;}}
  for(size_t i=0;i<shadowBounds.size()&&i<16;i++)lights.shadowBounds[i]=shadowBounds[i];d.writeFrameCB(lights);frameLights=lights;
  for(const auto& p:parts){if(p.effect==4)continue;
   float x=p.x,z=p.z,yaw=0;double remaining=seconds*p.speed;
   for(const auto& q:p.route){double dx=q.x-x,dz=q.y-z,len=sqrt(dx*dx+dz*dz);if(len>1e-8)yaw=(float)atan2(dx,dz);if(len>remaining&&len>0){x+=(float)(dx*remaining/len);z+=(float)(dz*remaining/len);break;}remaining-=len;x=q.x;z=q.y;}
   if(seconds<=0)yaw=0;
   auto transform=XMLoadFloat4x4(&p.local)*XMMatrixScaling(p.size,p.size,p.size)*XMMatrixRotationY(yaw)*XMMatrixTranslation(x,0,z);

   if(rt.active>0&&!p.source->verts.empty()&&p.source->verts[0].kind==9)transform=transform*XMMatrixTranslation(.012f*sinf(time*24),0,.012f*cosf(time*31));
   auto color=p.color;
   if(p.effect==1){float pulse=1+.12f*sinf(time*9+p.x)+.05f*sinf(time*17+p.z);transform=XMMatrixScaling(1,pulse,1)*transform;color.y*=.85f+.15f*sinf(time*7);}
   if(p.effect==2){float pulse=.88f+.12f*sinf(time*1.7f);color.x*=pulse;color.y*=pulse;}
   XMFLOAT4X4 world;XMStoreFloat4x4(&world,transform);rtItems.push_back({p.meshKey,p.source.get(),world,{color.x,color.y,color.z,p.emissive}});
   d.drawMesh(p.gpu,transform,color,p.emissive);
  }
 }
};
static thread_local std::string failure;
#define API extern "C" __declspec(dllexport)
API int sl_lightning(float x,float y,float z,float spread,float* output,int capacity){try{if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(spread)||spread<0||spread>5)return 0;auto mesh=starlight::Lightning({x,y,z},spread);if(output){if(capacity<(int)mesh.size())return 0;memcpy(output,mesh.data(),mesh.size()*sizeof(starlight::ArcVertex));}return (int)mesh.size();}catch(...){return 0;}}
API const char* sl_error(){return failure.c_str();}
// Optional ABI: old DLLs can be tested without importing unavailable symbols.
API int sl_benchmark_contract(){return 2;} // owned offscreen target; no swapchain writes
API void sl_benchmark(void* handle,int enable){auto& d=*static_cast<Viewport*>(handle)->renderer;d.setAuditOffscreen(enable!=0);}
API void sl_benchmark_flush(void* handle){static_cast<Viewport*>(handle)->renderer->flush();}
API void sl_scene_resources(void* handle,unsigned long long* parts,unsigned long long* blas,unsigned long long* uploads){auto& v=*static_cast<Viewport*>(handle);*parts=v.parts.size();*blas=v.rt.cache.size();*uploads=v.renderer->uploadKeepAlive.size();}
API void sl_upload_pool_stats(void* handle,unsigned long long* count,unsigned long long* bytes,unsigned long long* created,unsigned long long* reused){auto& d=*static_cast<Viewport*>(handle)->renderer;*count=d.uploadPool.size();*bytes=d.uploadPoolBytes;*created=d.uploadCreated;*reused=d.uploadReused;}
API void* sl_create(HWND window,int width,int height){
 try{
  wchar_t debugLog[32768];DWORD debugLen=GetEnvironmentVariableW(L"STARLIGHT_NATIVE_LOG",debugLog,32768);if(debugLen>0&&debugLen<32768&&!g_logFile)_wfopen_s(&g_logFile,debugLog,L"w");
  if(width<1||height<1)throw std::runtime_error("Invalid viewport size");
  auto v=std::make_unique<Viewport>();v->window=window;v->renderer=std::make_unique<RenderDevice>();auto& d=*v->renderer;
  d.d3dDebug=GetEnvironmentVariableW(L"STARLIGHT_D3D_DEBUG",nullptr,0)>0;d.linearHdrScene=true;d.frameRateLimit=500;d.vsync=false;d.upsRequest="off";d.modeRequest="native";d.fgRequest=false;d.cullOn=false;d.editorCameraDistance=13;
  Mesh cube;BuildBox(cube);d.init(window,width,height,cube);v->atmosphere.init(d);v->rt.init(d);auto raw=v.get();d.editorDraw=[raw]{raw->draw();};d.editorPost=[raw]{float time=raw->visualTime();bool animated=!raw->freeze&&(raw->cycle>0||raw->fogDensity>0||!raw->smoke.empty());for(auto& p:raw->parts)if(p.effect||(!p.source->verts.empty()&&p.source->verts[0].kind>=7))animated|=!raw->freeze;auto input=raw->rt.draw(*raw->renderer,raw->rtItems,raw->frameLights,time,raw->fogDensity,raw->smoke,animated);raw->atmosphere.draw(*raw->renderer,raw->frameLights,time,raw->fogDensity,raw->smoke,input,raw->rt.active>=3&&raw->rt.tlasBuilt?raw->rt.tlas->GetGPUVirtualAddress():0);};return v.release();
 }catch(const std::exception& e){failure=e.what();return nullptr;}
}
API int sl_resize(void* handle,int width,int height){
 try{
  if(!handle||width<1||height<1)throw std::runtime_error("Invalid viewport resize");
  auto& v=*static_cast<Viewport*>(handle);auto& d=*v.renderer;
  if(d.w==(UINT)width&&d.h==(UINT)height)return 1;
  d.resizeNativeOutput(width,height);v.rt.resize(d);v.atmosphere.resize(d);return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_scene(void* handle,const uint8_t* bytes,int length){
 try{
  if(!handle||!bytes||length<8)throw std::runtime_error("Missing viewport scene");
  Reader r{bytes,(size_t)length};auto version=r.get<uint32_t>();if(version!=0x31564c53&&version!=0x32564c53&&version!=0x33564c53&&version!=0x34564c53&&version!=0x35564c53&&version!=0x36564c53&&version!=0x37564c53&&version!=0x38564c53)throw std::runtime_error("Unsupported viewport scene version");
  auto count=r.get<uint32_t>();if(count>=kMaxInstances)throw std::runtime_error("Scene exceeds native draw capacity");
  std::vector<SpotLight> localLights;if(version>=0x32564c53){auto lightCount=r.get<uint32_t>();if(lightCount>32)throw std::runtime_error("Too many lights");localLights=r.array<SpotLight>(lightCount);}
  float hour=15,cycle=0,dayLength=180,density=.018f;std::vector<XMFLOAT4> smoke;if(version>=0x35564c53){hour=r.get<float>();cycle=r.get<float>();dayLength=r.get<float>();density=r.get<float>();if(!std::isfinite(hour)||!std::isfinite(cycle)||!std::isfinite(dayLength)||!std::isfinite(density)||hour<0||hour>24||dayLength<10||density<0||density>.3f)throw std::runtime_error("Invalid atmosphere");auto n=r.get<uint32_t>();if(n>16)throw std::runtime_error("Too many smoke volumes");smoke=r.array<XMFLOAT4>(n);}
  int rtTier=0,rtBounces=3,rtFreeze=0;if(version>=0x36564c53){rtTier=r.get<int>();rtBounces=r.get<int>();rtFreeze=r.get<int>();if(rtTier<0||rtTier>4||rtBounces<2||rtBounces>4||rtFreeze<0||rtFreeze>1)throw std::runtime_error("Invalid raytracing settings");}
  int rrMode=1;if(version>=0x37564c53){rrMode=r.get<int>();if(rrMode<0||rrMode>1)throw std::runtime_error("Invalid denoiser setting");}
  struct Incoming {Part part;Mesh mesh;};std::vector<Incoming> incoming;
  for(uint32_t i=0;i<count;i++){
   Incoming in;if(version>=0x38564c53){auto transient=r.get<uint32_t>();if(transient>1)throw std::runtime_error("Invalid transient geometry flag");in.part.streaming=transient && rtTier==0;}if(version>=0x32564c53){in.part.emissive=r.get<float>();in.part.effect=r.get<int>();in.part.baseY=r.get<float>();}in.part.color=r.get<XMFLOAT3>();in.part.x=r.get<float>();in.part.z=r.get<float>();in.part.size=r.get<float>();in.part.speed=r.get<float>();
   in.part.route=r.array<XMFLOAT2>(r.get<uint32_t>());if(version>=0x33564c53)in.part.local=r.get<XMFLOAT4X4>();auto vertices=r.get<uint32_t>();if(version>=0x34564c53)in.mesh.verts=r.array<Vertex>(vertices);else{struct OldVertex{float x,y,z,nx,ny,nz;};for(auto p:r.array<OldVertex>(vertices))in.mesh.verts.push_back({p.x,p.y,p.z,p.nx,p.ny,p.nz});}in.mesh.indices=r.array<uint32_t>(r.get<uint32_t>());
   auto finite=[](const float* p,size_t count){for(size_t k=0;k<count;k++)if(!std::isfinite(p[k]))return false;return true;};
   if(!finite(&in.part.color.x,3)||!finite(&in.part.local._11,16)||!std::isfinite(in.part.x)||!std::isfinite(in.part.z)||!std::isfinite(in.part.size)||!std::isfinite(in.part.speed)||!std::isfinite(in.part.emissive)||!std::isfinite(in.part.baseY)||in.part.size<=0)throw std::runtime_error("Invalid mesh transform/material");
   for(auto p:in.part.route)if(!std::isfinite(p.x)||!std::isfinite(p.y))throw std::runtime_error("Invalid route coordinate");
   for(const auto& vertex:in.mesh.verts)if(!finite(&vertex.px,sizeof(Vertex)/sizeof(float)))throw std::runtime_error("Non-finite mesh vertex");
   if(in.mesh.indices.size()%3)throw std::runtime_error("Incomplete mesh triangle");
   if(in.mesh.verts.empty()||in.mesh.indices.empty())throw std::runtime_error("Empty mesh");
   for(auto index:in.mesh.indices)if(index>=in.mesh.verts.size())throw std::runtime_error("Invalid mesh index");
   uint64_t hash=1469598103934665603ull;auto mix=[&](const void* bytes,size_t count){auto ptr=(const uint8_t*)bytes;for(size_t j=0;j<count;j++){hash^=ptr[j];hash*=1099511628211ull;}};
   mix(&in.part.streaming,sizeof(in.part.streaming));mix(in.mesh.verts.data(),in.mesh.verts.size()*sizeof(Vertex));mix(in.mesh.indices.data(),in.mesh.indices.size()*sizeof(uint32_t));in.part.meshKey=hash;incoming.push_back(std::move(in));
  }
  if(r.left)throw std::runtime_error("Unexpected viewport scene data");
  auto& v=*static_cast<Viewport*>(handle);auto& d=*v.renderer;
  v.shadowBounds.clear();for(const auto& in:incoming){if(in.part.speed<=0||in.part.emissive>0||in.mesh.verts.empty()||in.mesh.verts[0].kind==1)continue;XMFLOAT3 lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};auto matrix=XMLoadFloat4x4(&in.part.local)*XMMatrixScaling(in.part.size,in.part.size,in.part.size)*XMMatrixTranslation(in.part.x,0,in.part.z);for(const auto& vertex:in.mesh.verts){XMFLOAT3 p;XMStoreFloat3(&p,XMVector3TransformCoord(XMVectorSet(vertex.px,vertex.py,vertex.pz,1),matrix));lo.x=std::min(lo.x,p.x);lo.y=std::min(lo.y,p.y);lo.z=std::min(lo.z,p.z);hi.x=std::max(hi.x,p.x);hi.y=std::max(hi.y,p.y);hi.z=std::max(hi.z,p.z);}float radius=std::min((hi.y-lo.y)*.48f,std::max(hi.x-lo.x,hi.z-lo.z)*.35f);if(radius>.01f&&v.shadowBounds.size()<16)v.shadowBounds.emplace_back((lo.x+hi.x)*.5f,lo.y+radius,(lo.z+hi.z)*.5f,radius);}
  std::unordered_map<uint64_t,std::shared_ptr<Mesh>> sources;for(auto& p:v.parts)sources.emplace(p.meshKey,p.source);
  std::unordered_map<uint64_t,GpuMesh> cache;for(auto& p:v.parts)cache.emplace(p.meshKey,p.gpu);
  bool changed=false;for(auto& in:incoming)if(!in.part.streaming && !cache.count(in.part.meshKey))changed=true;
  if(changed){d.flush();d.alloc->Reset();d.cmd->Reset(d.alloc.Get(),nullptr);}
  std::vector<Part> next;for(auto& in:incoming){auto found=cache.find(in.part.meshKey);if(found==cache.end()){in.part.gpu=in.part.streaming?v.streamMesh(in.mesh):d.uploadMesh(in.mesh);v.uploads++;cache.emplace(in.part.meshKey,in.part.gpu);}else in.part.gpu=found->second;auto source=sources.find(in.part.meshKey);if(source!=sources.end())in.part.source=source->second;else{in.part.source=std::make_shared<Mesh>(std::move(in.mesh));sources.emplace(in.part.meshKey,in.part.source);}next.push_back(std::move(in.part));}
  if(changed){d.cmd->Close();ID3D12CommandList* lists[]={d.cmd.Get()};d.queue->ExecuteCommandLists(1,lists);d.flush();d.retireUploads();}
  // Retire removed buffers only after their previous frame has completed.
  bool removed=false;for(const auto& old:v.parts)if(!old.streaming && std::none_of(next.begin(),next.end(),[&](const Part& p){return p.meshKey==old.meshKey;}))removed=true;
  if(removed&&!changed)d.flush();
  if(removed){v.rt.retireGeometry(d);v.rtItems.clear();d.prevModelByKey.clear();d.curModelByKey.clear();}
  for(auto& old:cache)if(std::none_of(next.begin(),next.end(),[&](const Part& p){return p.meshKey==old.first;})){
   auto original=std::find_if(v.parts.begin(),v.parts.end(),[&](const Part& p){return p.meshKey==old.first;});
   if(original!=v.parts.end() && original->streaming)v.streams.push_back({old.second,d.fenceVal});
   else{d.recycleBuffer(old.second.vb);d.recycleBuffer(old.second.ib);}
  }
  v.parts=std::move(next);v.localLights=std::move(localLights);v.hour=hour;v.cycle=cycle;v.dayLength=dayLength;v.fogDensity=density;v.smoke=std::move(smoke);v.rt.denoiserMode=rrMode;v.rt.requested=rtTier;v.rt.bounces=rtBounces;if(rtFreeze&&!v.freeze)v.frozenTime=v.visualTime();v.freeze=rtFreeze!=0;d.resetHistory=true;return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_frame(void* handle,double seconds,float distance){
 try{if(!handle||!std::isfinite(seconds)||!std::isfinite(distance))throw std::runtime_error("Invalid frame input");auto& v=*static_cast<Viewport*>(handle);v.seconds=seconds;v.renderer->editorTime=v.visualTime();v.renderer->editorCameraDistance=distance;std::vector<DxrItem> geometry;for(auto& p:v.parts)if(p.effect!=4)geometry.push_back({p.meshKey,p.source.get(),{}, {}});try{v.rt.prepare(*v.renderer,geometry);}catch(const std::exception& ex){v.rt.active=0;v.rt.supported=false;v.rt.status=std::string("DXR initialization failed; raster fallback: ")+ex.what();}v.renderer->editorRaytracing=v.rt.active>0;v.renderer->renderEmpty(v.window);if(FAILED(v.renderer->device->GetDeviceRemovedReason())){v.renderer->reportDeviceLoss();throw std::runtime_error("Native graphics device lost");}return 1;}
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
API void sl_destroy(void* handle){if(!handle)return;auto v=static_cast<Viewport*>(handle);v->renderer->flush();v->rt.shutdown();v->renderer->teardown();delete v;}



API void sl_rt(void* handle,int tier,int bounces){auto& rt=static_cast<Viewport*>(handle)->rt;rt.requested=std::clamp(tier,0,4);rt.bounces=std::clamp(bounces,2,4);rt.samples=0;}
API void sl_freeze(void* handle,int freeze){auto& v=*static_cast<Viewport*>(handle);v.freeze=freeze!=0;v.frozenTime=0;}
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
  d.flush();auto description=rt.images[index]->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT layout{};UINT rows;UINT64 rowBytes,bytes;
  d.device->GetCopyableFootprints(&description,0,1,0,&layout,&rows,&rowBytes,&bytes);
  auto readback=rt.buffer(bytes,D3D12_HEAP_TYPE_READBACK,D3D12_RESOURCE_STATE_COPY_DEST);
  d.alloc->Reset();d.cmd->Reset(d.alloc.Get(),nullptr);rt.imageState(d,index,D3D12_RESOURCE_STATE_COPY_SOURCE);
  D3D12_TEXTURE_COPY_LOCATION src{};src.pResource=rt.images[index].Get();src.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
  D3D12_TEXTURE_COPY_LOCATION dst{};dst.pResource=readback.Get();dst.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;dst.PlacedFootprint=layout;
  d.cmd->CopyTextureRegion(&dst,0,0,0,&src,nullptr);rt.imageState(d,index,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
  d.cmd->Close();ID3D12CommandList* lists[]={d.cmd.Get()};d.queue->ExecuteCommandLists(1,lists);d.flush();
  uint8_t* data=nullptr;D3D12_RANGE range{0,(SIZE_T)bytes};if(FAILED(readback->Map(0,&range,(void**)&data)))return 0;
  std::ofstream file(path,std::ios::binary);uint32_t header[4]={(UINT)description.Width,description.Height,(UINT)description.Format,(UINT)rowBytes};file.write((char*)header,sizeof(header));for(UINT row=0;row<rows;row++)file.write((char*)data+layout.Offset+row*layout.Footprint.RowPitch,rowBytes);
  D3D12_RANGE none{0,0};readback->Unmap(0,&none);return file.good()?1:0;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
