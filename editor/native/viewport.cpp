#ifdef STARLIGHT_CMAKE_RENDERER
#include <render_device_editor.h>
#else
#include "render_device_editor.h"
#endif
#include "../../deep/engine/atmosphere.h"
#include "../../deep/engine/chaos_lightning.h"
#include <cstring>
#include <chrono>
#include <fstream>
#include "../../deep/engine/dxr.h"

struct Reader {
 const uint8_t* p; size_t left;
 template<class T>T get(){if(left<sizeof(T))throw std::runtime_error("Incomplete viewport scene");T v;memcpy(&v,p,sizeof(v));p+=sizeof(v);left-=sizeof(v);return v;}
 template<class T>std::vector<T> array(uint32_t n){if(n>left/sizeof(T))throw std::runtime_error("Invalid viewport array");std::vector<T> v(n);memcpy(v.data(),p,n*sizeof(T));p+=n*sizeof(T);left-=n*sizeof(T);return v;}
};
struct Part { XMFLOAT3 fogLow{},fogHigh{};bool fogSolid=false;std::shared_ptr<Mesh> source;GpuMesh gpu; uint64_t meshKey=0,motionKey=0; bool streaming=false;std::vector<uint64_t> vertexIdentities; XMFLOAT3 color; float x,z,size,speed; float emissive=0,baseY=0;int effect=0; XMFLOAT4X4 local{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}; std::vector<XMFLOAT2> route; };
struct Viewport {
 DxrPass rt;std::vector<DxrItem> rtItems;bool freeze=false,externalEffectClock=false;float frozenTime=0,effectClock=0;
 float visualTime()const{return freeze?frozenTime:externalEffectClock?effectClock:std::chrono::duration<float>(std::chrono::steady_clock::now()-started).count();}
 bool geometryFresh=false;
 std::unordered_map<uint64_t,XMFLOAT3> previousVertices,currentVertices;
 bool externalFogObstacles=false;
 AtmospherePass atmosphere;FrameCB frameLights{};float hour=15,cycle=0,dayLength=180,fogDensity=.018f;std::vector<XMFLOAT4> smoke;std::chrono::steady_clock::time_point started=std::chrono::steady_clock::now();
 std::unique_ptr<RenderDevice> renderer;
 std::vector<uint64_t> telegraphIdentities;
 std::vector<Part> parts;std::vector<SpotLight> localLights;std::vector<XMFLOAT4> shadowBounds;

 struct TelegraphInstance {float x,y,z,aimX,aimZ,range,halfAngle,progress,r,g,b;};
 struct TelegraphTemplate {float halfAngle;int rings;GpuMesh gpu;};
 std::vector<TelegraphInstance> telegraphs;
 std::vector<TelegraphTemplate> telegraphTemplates;
 UINT64 telegraphBytes=0;
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
  d.flush();d.alloc->Reset();d.cmd->Reset(d.alloc.Get(),nullptr);
  auto gpu=d.uploadMesh(mesh);d.cmd->Close();ID3D12CommandList* lists[]={d.cmd.Get()};d.queue->ExecuteCommandLists(1,lists);d.flush();d.retireUploads();
  telegraphBytes+=gpu.vb->GetDesc().Width+gpu.ib->GetDesc().Width;
  telegraphTemplates.push_back({halfAngle,rings,std::move(gpu)});
 }
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
 float previousEffectTime=0;bool hasEffectTime=false;
 double seconds=0;uint64_t uploads=0,transientIdentity=0;
 HWND window=nullptr;
 std::string error;
 void draw(){
  auto& d=*renderer;currentVertices.clear();rtItems.clear();if(!externalFogObstacles){atmosphere.obstacleLow.clear();atmosphere.obstacleHigh.clear();atmosphere.obstacleVelocity.clear();}
  float time=visualTime();
  float angle=(hour+(cycle>0?time/dayLength*24:0)-6)*3.14159265f/12;float elevation=sinf(angle),day=std::clamp(elevation*2,0.f,1.f);
  FrameCB lights{};lights.motionTime={time,hasEffectTime?previousEffectTime:time,geometryFresh?1.f:0.f,0};previousEffectTime=time;hasEffectTime=true;lights.lightDir=XMFLOAT4(cosf(angle),-std::max(.03f,elevation),-.35f,day*3.8f);lights.ambient=XMFLOAT4(.025f+day*.355f,.018f+day*.442f,.07f+day*.50f,.16f+day*.12f);lights.camPos=XMFLOAT4(d.camWorld.x,d.camWorld.y,d.camWorld.z,1);lights.fog=XMFLOAT4(.12f,.18f,.24f,fogDensity);
  for(size_t i=0;i<localLights.size()&&i<32;i++){lights.spots[i]=localLights[i];if(lights.spots[i].dir.w<-2.5f){float pulse=.72f+.28f*sinf(time*37+floorf(time*19)*3.1f);lights.spots[i].color.x*=pulse;lights.spots[i].color.y*=pulse;lights.spots[i].color.z*=pulse;}}
  for(size_t i=0;i<shadowBounds.size()&&i<16;i++)lights.shadowBounds[i]=shadowBounds[i];d.writeFrameCB(lights);frameLights=lights;
  for(const auto& p:parts){if(p.effect==4)continue;
   float x=p.x,z=p.z,yaw=0;double remaining=seconds*p.speed;
   for(const auto& q:p.route){double dx=q.x-x,dz=q.y-z,len=sqrt(dx*dx+dz*dz);if(len>1e-8)yaw=(float)atan2(dx,dz);if(len>remaining&&len>0){x+=(float)(dx*remaining/len);z+=(float)(dz*remaining/len);break;}remaining-=len;x=q.x;z=q.y;}
   if(seconds<=0)yaw=0;
   auto transform=XMLoadFloat4x4(&p.local)*XMMatrixScaling(p.size,p.size,p.size)*XMMatrixRotationY(yaw)*XMMatrixTranslation(x,0,z);

   if(!externalFogObstacles && p.fogSolid && atmosphere.obstacleLow.size()<128){
    XMFLOAT3 lo{1e20f,1e20f,1e20f},hi{-1e20f,-1e20f,-1e20f};
    for(int k=0;k<8;++k){XMFLOAT3 q;XMStoreFloat3(&q,XMVector3TransformCoord(XMVectorSet(k&1?p.fogHigh.x:p.fogLow.x,k&2?p.fogHigh.y:p.fogLow.y,k&4?p.fogHigh.z:p.fogLow.z,1),transform));lo.x=std::min(lo.x,q.x);lo.y=std::min(lo.y,q.y);lo.z=std::min(lo.z,q.z);hi.x=std::max(hi.x,q.x);hi.y=std::max(hi.y,q.y);hi.z=std::max(hi.z,q.z);}
    atmosphere.obstacleLow.emplace_back(lo.x,lo.y,lo.z,0);atmosphere.obstacleHigh.emplace_back(hi.x,hi.y,hi.z,0);
   }
   if(rt.active>0&&!p.source->verts.empty()&&p.source->verts[0].kind==9)transform=transform*XMMatrixTranslation(.012f*sinf(time*24),0,.012f*cosf(time*31));
   auto color=p.color;
   if(p.effect==1){float pulse=1+.12f*sinf(time*9+p.x)+.05f*sinf(time*17+p.z);transform=XMMatrixScaling(1,pulse,1)*transform;color.y*=.85f+.15f*sinf(time*7);}
   if(p.effect==2){float pulse=.88f+.12f*sinf(time*1.7f);color.x*=pulse;color.y*=pulse;}
   XMFLOAT4X4 world;XMStoreFloat4x4(&world,transform);rtItems.push_back({p.meshKey,p.source.get(),world,{color.x,color.y,color.z,p.emissive},p.motionKey});
   for(size_t i=0;i<p.vertexIdentities.size();++i)if(auto id=p.vertexIdentities[i]){const auto& point=p.source->verts[i];currentVertices[id]={point.px,point.py,point.pz};}
   d.editorMotionKey=p.motionKey;d.drawMesh(p.gpu,transform,color,p.emissive);d.editorMotionKey=0;
  }
  size_t telegraphIndex=0;for(const auto& effect:telegraphs){uint64_t identity=telegraphIndex<telegraphIdentities.size()?telegraphIdentities[telegraphIndex]:0;++telegraphIndex;
   const int rings=std::max(1,int(ceilf(effect.range/.5f)));
   auto found=std::find_if(telegraphTemplates.begin(),telegraphTemplates.end(),[&](const auto& t){return t.rings==rings && t.halfAngle==effect.halfAngle;});
   if(found==telegraphTemplates.end())continue;
   const float radius=effect.range*effect.progress,yaw=atan2f(-effect.aimX,-effect.aimZ);
   auto transform=XMMatrixScaling(radius,1,radius)*XMMatrixRotationY(yaw)*XMMatrixTranslation(effect.x,effect.y,effect.z);
   d.editorMotionKey=identity;d.drawMesh(found->gpu,transform,{effect.r,effect.g,effect.b},1);d.editorMotionKey=0;
  }
  previousVertices.swap(currentVertices);geometryFresh=false;
 }
};
static thread_local std::string failure;
#define API extern "C" __declspec(dllexport)
API int sl_lightning(float x,float y,float z,float spread,float* output,int capacity){try{if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||!std::isfinite(spread)||spread<0||spread>5)return 0;auto mesh=starlight::Lightning({x,y,z},spread);if(output){if(capacity<(int)mesh.size())return 0;memcpy(output,mesh.data(),mesh.size()*sizeof(starlight::ArcVertex));}return (int)mesh.size();}catch(...){return 0;}}
API int sl_motion_readback(void* handle,double* output){
 try{if(!handle||!output)throw std::runtime_error("Invalid motion readback");auto& viewport=*static_cast<Viewport*>(handle);auto& d=*viewport.renderer;auto motion=viewport.rt.active>0?viewport.rt.images[25].Get():d.sceneMvec.Get();if(!motion)throw std::runtime_error("Motion texture unavailable");d.flush();auto desc=motion->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows;UINT64 rowBytes,total;d.device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&total);
 D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> readback;d.must(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"Motion readback allocation");
 d.alloc->Reset();d.cmd->Reset(d.alloc.Get(),nullptr);d.barrier(motion,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION destination{};destination.pResource=readback.Get();destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;destination.PlacedFootprint=footprint;D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=motion;source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;d.cmd->CopyTextureRegion(&destination,0,0,0,&source,nullptr);d.barrier(motion,D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);d.cmd->Close();ID3D12CommandList* lists[]={d.cmd.Get()};d.queue->ExecuteCommandLists(1,lists);d.flush();
 uint8_t* mapped=nullptr;D3D12_RANGE range{0,size_t(total)};d.must(readback->Map(0,&range,reinterpret_cast<void**>(&mapped)),"Map motion readback");
 auto half=[](uint16_t value){int exponent=(value>>10)&31,mantissa=value&1023;double result=exponent==0?std::ldexp(double(mantissa),-24):exponent==31?INFINITY:std::ldexp(double(mantissa+1024),exponent-25);return value&32768?-result:result;};
 double peak=0,sum=0,moving=0;for(UINT y=0;y<rows;++y){auto* row=reinterpret_cast<const uint16_t*>(mapped+footprint.Offset+size_t(y)*footprint.Footprint.RowPitch);for(UINT x=0;x<desc.Width;++x){double magnitude=std::hypot(half(row[x*2]),half(row[x*2+1]));if(!std::isfinite(magnitude)){readback->Unmap(0,nullptr);throw std::runtime_error("Nonfinite rendered motion vector");}peak=std::max(peak,magnitude);sum+=magnitude;moving+=magnitude>.001;}}
 D3D12_RANGE written{0,0};readback->Unmap(0,&written);output[0]=peak;output[1]=sum/(double(desc.Width)*rows);output[2]=moving;return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_fog_readback(void* handle,double* output){
 try{if(!handle||!output)throw std::runtime_error("Invalid fog readback");auto& v=*static_cast<Viewport*>(handle);auto& d=*v.renderer;auto field=v.atmosphere.fogField[v.atmosphere.fogIndex];if(!field)throw std::runtime_error("Fog field is not initialized");d.flush();auto desc=field->GetDesc();D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT rows;UINT64 rowBytes,total;d.device->GetCopyableFootprints(&desc,0,1,0,&footprint,&rows,&rowBytes,&total);
 D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_READBACK;D3D12_RESOURCE_DESC buffer{};buffer.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;buffer.Width=total;buffer.Height=1;buffer.DepthOrArraySize=1;buffer.MipLevels=1;buffer.SampleDesc.Count=1;buffer.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;ComPtr<ID3D12Resource> readback;d.must(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&buffer,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)),"Fog readback allocation");
 d.alloc->Reset();d.cmd->Reset(d.alloc.Get(),nullptr);d.barrier(field.Get(),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_SOURCE);D3D12_TEXTURE_COPY_LOCATION destination{};destination.pResource=readback.Get();destination.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;destination.PlacedFootprint=footprint;D3D12_TEXTURE_COPY_LOCATION source{};source.pResource=field.Get();source.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;d.cmd->CopyTextureRegion(&destination,0,0,0,&source,nullptr);d.barrier(field.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);d.cmd->Close();ID3D12CommandList* lists[]={d.cmd.Get()};d.queue->ExecuteCommandLists(1,lists);d.flush();
 uint8_t* mapped=nullptr;D3D12_RANGE range{0,size_t(total)};d.must(readback->Map(0,&range,reinterpret_cast<void**>(&mapped)),"Map fog readback");
 auto half=[](uint16_t value){int exponent=(value>>10)&31,mantissa=value&1023;double result=exponent==0?std::ldexp(double(mantissa),-24):exponent==31?INFINITY:std::ldexp(double(mantissa+1024),exponent-25);return value&32768?-result:result;};
 double mass=0,dark=0,cx=0,cy=0,cz=0,solid=0,speed=0;
 for(UINT z=0;z<footprint.Footprint.Depth;++z)for(UINT y=0;y<rows;++y){auto* row=reinterpret_cast<const uint16_t*>(mapped+footprint.Offset+(size_t(z)*rows+y)*footprint.Footprint.RowPitch);for(UINT x=0;x<desc.Width;++x){double mist=half(row[x*4]),smoke=half(row[x*4+1]),vx=half(row[x*4+2]),vz=half(row[x*4+3]);if(!std::isfinite(mist+smoke+vx+vz)){readback->Unmap(0,nullptr);throw std::runtime_error("Nonfinite fog field");}double px=-16+(x+.5)*.5,py=-2+(y+.5)*.5,pz=-16+(z+.5)*.5;mass+=mist+smoke;dark+=smoke;cx+=smoke*px;cy+=smoke*py;cz+=smoke*pz;speed=std::max(speed,std::hypot(vx,vz));bool buried=py<=v.atmosphere.terrainHeights[z*64+x];if(buried)solid+=mist+smoke;for(size_t i=0;i<v.atmosphere.obstacleLow.size();++i){auto lo=v.atmosphere.obstacleLow[i],hi=v.atmosphere.obstacleHigh[i];if(!buried&&px>=lo.x&&px<=hi.x&&py>=lo.y&&py<=hi.y&&pz>=lo.z&&pz<=hi.z){solid+=mist+smoke;break;}}}}

 D3D12_RANGE written{0,0};readback->Unmap(0,&written);output[0]=mass;output[1]=dark;output[2]=dark>0?cx/dark:0;output[3]=dark>0?cy/dark:0;output[4]=dark>0?cz/dark:0;output[5]=solid;output[6]=speed;return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_environment_lights(void* handle,const float* data,int count){
 try{if(!handle||count<0||count>32||(count&&!data))throw std::runtime_error("Invalid environment lights");std::vector<SpotLight> lights;for(int i=0;i<count;++i){auto p=data+i*12;for(int j=0;j<12;++j)if(!std::isfinite(p[j]))throw std::runtime_error("Nonfinite environment light");lights.push_back({{p[0],p[1],p[2],p[3]},{p[4],p[5],p[6],p[7]},{p[8],p[9],p[10],p[11]}});}static_cast<Viewport*>(handle)->localLights=std::move(lights);return 1;}catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_fog_sources(void* handle,const float* data,int count){
 try{if(!handle||count<0||count>16||(count&&!data))throw std::runtime_error("Invalid smoke sources");std::vector<XMFLOAT4> sources;for(int i=0;i<count;++i){auto p=data+i*4;for(int j=0;j<4;++j)if(!std::isfinite(p[j]))throw std::runtime_error("Nonfinite smoke source");if(p[3]<=0)throw std::runtime_error("Invalid smoke radius");sources.emplace_back(p[0],p[1],p[2],p[3]);}static_cast<Viewport*>(handle)->smoke=std::move(sources);return 1;}catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_fog_terrain(void* handle,const float* heights){
 try{if(!handle||!heights)throw std::runtime_error("Invalid fog terrain");auto& a=static_cast<Viewport*>(handle)->atmosphere;for(int i=0;i<4096;++i){if(!std::isfinite(heights[i]))throw std::runtime_error("Nonfinite fog terrain");a.terrainHeights[i]=heights[i];}return 1;}catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_fog_obstacles(void* handle,const float* data,int count){
 try{if(!handle||count<0||count>128||(count&&!data))throw std::runtime_error("Invalid fog obstacles");auto& v=*static_cast<Viewport*>(handle);v.externalFogObstacles=true;auto& a=v.atmosphere;a.obstacleLow.clear();a.obstacleHigh.clear();a.obstacleVelocity.clear();
 for(int i=0;i<count;++i){const auto* p=data+i*9;for(int j=0;j<9;++j)if(!std::isfinite(p[j]))throw std::runtime_error("Nonfinite fog obstacle");a.obstacleLow.emplace_back(p[0],p[1],p[2],0);a.obstacleHigh.emplace_back(p[3],p[4],p[5],0);a.obstacleVelocity.emplace_back(p[6],p[7],p[8],0);}return 1;
 }catch(const std::exception& e){if(handle)static_cast<Viewport*>(handle)->error=e.what();return 0;}
}
API void sl_environment_reset(void* handle){auto& v=*static_cast<Viewport*>(handle);v.atmosphere.reset();v.rt.previousWorldByKey.clear();v.hasEffectTime=false;v.renderer->prevModelByKey.clear();v.renderer->curModelByKey.clear();v.renderer->resetHistory=true;v.previousVertices.clear();v.currentVertices.clear();}
API int sl_effect_clock(void* handle,double seconds){if(!handle||!std::isfinite(seconds)||seconds<0||seconds>FLT_MAX)return 0;auto& v=*static_cast<Viewport*>(handle);v.externalEffectClock=true;v.effectClock=float(seconds);return 1;}
API float sl_effect_time(void* handle){return static_cast<Viewport*>(handle)->previousEffectTime;}
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
  d.editorKeyedHistory=true;d.d3dDebug=GetEnvironmentVariableW(L"STARLIGHT_D3D_DEBUG",nullptr,0)>0;d.linearHdrScene=true;d.frameRateLimit=500;d.vsync=false;d.upsRequest="off";d.modeRequest="native";d.fgRequest=false;d.cullOn=false;d.editorCameraDistance=13;
  Mesh cube;BuildBox(cube);d.init(window,width,height,cube);v->atmosphere.init(d);v->rt.init(d);v->prepareTelegraph(.785398163f,6);v->prepareTelegraph(float(.2617993877991494),40);auto raw=v.get();d.editorDraw=[raw]{raw->draw();};d.editorPost=[raw]{float time=raw->visualTime();bool animated=!raw->freeze&&(raw->cycle>0||raw->fogDensity>0||!raw->smoke.empty());for(auto& p:raw->parts)if(p.effect||(!p.source->verts.empty()&&p.source->verts[0].kind>=7))animated|=!raw->freeze;auto input=raw->rt.draw(*raw->renderer,raw->rtItems,raw->frameLights,time,raw->fogDensity,raw->smoke,animated);raw->atmosphere.draw(*raw->renderer,raw->frameLights,time,raw->fogDensity,raw->smoke,input,raw->rt.active>=3&&raw->rt.tlasBuilt?raw->rt.tlas->GetGPUVirtualAddress():0);};return v.release();
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
  auto& v=*static_cast<Viewport*>(handle);auto& d=*v.renderer;
  Reader r{bytes,(size_t)length};auto version=r.get<uint32_t>();if(version!=0x31564c53&&version!=0x32564c53&&version!=0x33564c53&&version!=0x34564c53&&version!=0x35564c53&&version!=0x36564c53&&version!=0x37564c53&&version!=0x38564c53&&version!=0x39564c53&&version!=0x41564c53)throw std::runtime_error("Unsupported viewport scene version");
  auto count=r.get<uint32_t>();if(count>=kMaxInstances)throw std::runtime_error("Scene exceeds native draw capacity");
  std::vector<SpotLight> localLights;if(version>=0x32564c53){auto lightCount=r.get<uint32_t>();if(lightCount>32)throw std::runtime_error("Too many lights");localLights=r.array<SpotLight>(lightCount);}
  float hour=15,cycle=0,dayLength=180,density=.018f;std::vector<XMFLOAT4> smoke;if(version>=0x35564c53){hour=r.get<float>();cycle=r.get<float>();dayLength=r.get<float>();density=r.get<float>();if(!std::isfinite(hour)||!std::isfinite(cycle)||!std::isfinite(dayLength)||!std::isfinite(density)||hour<0||hour>24||dayLength<10||density<0||density>.3f)throw std::runtime_error("Invalid atmosphere");auto n=r.get<uint32_t>();if(n>16)throw std::runtime_error("Too many smoke volumes");smoke=r.array<XMFLOAT4>(n);}
  int rtTier=0,rtBounces=3,rtFreeze=0;if(version>=0x36564c53){rtTier=r.get<int>();rtBounces=r.get<int>();rtFreeze=r.get<int>();if(rtTier<0||rtTier>4||rtBounces<2||rtBounces>4||rtFreeze<0||rtFreeze>1)throw std::runtime_error("Invalid raytracing settings");}
  int rrMode=1;if(version>=0x37564c53){rrMode=r.get<int>();if(rrMode<0||rrMode>1)throw std::runtime_error("Invalid denoiser setting");}
  struct Incoming {Part part;Mesh mesh;};std::vector<Incoming> incoming;
  for(uint32_t i=0;i<count;i++){
   Incoming in;if(version>=0x39564c53)in.part.motionKey=r.get<uint64_t>();if(version>=0x38564c53){auto transient=r.get<uint32_t>();if(transient>1)throw std::runtime_error("Invalid transient geometry flag");in.part.streaming=transient && rtTier==0;}if(version>=0x32564c53){in.part.emissive=r.get<float>();in.part.effect=r.get<int>();in.part.baseY=r.get<float>();}in.part.color=r.get<XMFLOAT3>();in.part.x=r.get<float>();in.part.z=r.get<float>();in.part.size=r.get<float>();in.part.speed=r.get<float>();
   in.part.route=r.array<XMFLOAT2>(r.get<uint32_t>());if(version>=0x33564c53)in.part.local=r.get<XMFLOAT4X4>();auto vertices=r.get<uint32_t>();if(version>=0x34564c53){struct PriorVertex{float px,py,pz,nx,ny,nz,r,g,b,kind;};for(uint32_t i=0;i<vertices;++i){auto p=r.get<PriorVertex>();Vertex vertex{p.px,p.py,p.pz,p.nx,p.ny,p.nz,p.r,p.g,p.b,p.kind};uint64_t id=version>=0x41564c53?r.get<uint64_t>():0;in.part.vertexIdentities.push_back(id);if(id)if(auto previous=v.previousVertices.find(id);previous!=v.previousVertices.end()){vertex.previousX=previous->second.x;vertex.previousY=previous->second.y;vertex.previousZ=previous->second.z;vertex.hasPrevious=1;}in.mesh.verts.push_back(vertex);}}else{struct OldVertex{float x,y,z,nx,ny,nz;};for(auto p:r.array<OldVertex>(vertices))in.mesh.verts.push_back({p.x,p.y,p.z,p.nx,p.ny,p.nz});}in.mesh.indices=r.array<uint32_t>(r.get<uint32_t>());
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
    uint64_t hash=1469598103934665603ull;auto mix=[&](const void* bytes,size_t count){auto ptr=(const uint8_t*)bytes;for(size_t j=0;j<count;j++){hash^=ptr[j];hash*=1099511628211ull;}};
    mix(in.mesh.verts.data(),in.mesh.verts.size()*sizeof(Vertex));mix(in.mesh.indices.data(),in.mesh.indices.size()*sizeof(uint32_t));in.part.meshKey=hash&0x7fffffffffffffffull;
   }
   float kind=in.mesh.verts.front().kind;
   in.part.fogSolid=in.part.speed>0 && in.part.emissive==0 && (kind==0 || kind==6 || kind==10);
   if(in.part.fogSolid){in.part.fogLow={1e20f,1e20f,1e20f};in.part.fogHigh={-1e20f,-1e20f,-1e20f};for(const auto& q:in.mesh.verts){auto& lo=in.part.fogLow;auto& hi=in.part.fogHigh;lo.x=std::min(lo.x,q.px);lo.y=std::min(lo.y,q.py);lo.z=std::min(lo.z,q.pz);hi.x=std::max(hi.x,q.px);hi.y=std::max(hi.y,q.py);hi.z=std::max(hi.z,q.pz);}}
   incoming.push_back(std::move(in));
  }
  if(r.left)throw std::runtime_error("Unexpected viewport scene data");
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
  if(removed){v.rt.retireGeometry(d);v.rtItems.clear();}
  for(auto& old:cache)if(std::none_of(next.begin(),next.end(),[&](const Part& p){return p.meshKey==old.first;})){
   auto original=std::find_if(v.parts.begin(),v.parts.end(),[&](const Part& p){return p.meshKey==old.first;});
   if(original!=v.parts.end() && original->streaming)v.streams.push_back({old.second,d.fenceVal});
   else{d.recycleBuffer(old.second.vb);d.recycleBuffer(old.second.ib);}
  }
  v.parts=std::move(next);v.geometryFresh=true;v.localLights=std::move(localLights);v.hour=hour;v.cycle=cycle;v.dayLength=dayLength;v.fogDensity=density;v.smoke=std::move(smoke);v.rt.denoiserMode=rrMode;v.rt.requested=rtTier;v.rt.bounces=rtBounces;if(rtFreeze&&!v.freeze)v.frozenTime=v.visualTime();v.freeze=rtFreeze!=0;if(version<0x39564c53)d.resetHistory=true;return 1;
 }catch(const std::exception& e){failure=e.what();return 0;}
}
API int sl_telegraphs(void* handle,const float* data,int count){
 try{
  if(!handle || count<0 || count>4096 || (count && !data))throw std::runtime_error("Invalid GPU telegraph instances");
  auto& v=*static_cast<Viewport*>(handle);std::vector<Viewport::TelegraphInstance> next(count);
  static_assert(sizeof(Viewport::TelegraphInstance)==11*sizeof(float));
  if(count)memcpy(next.data(),data,size_t(count)*sizeof(Viewport::TelegraphInstance));
  for(const auto& e:next)for(int i=0;i<11;++i)if(!std::isfinite(reinterpret_cast<const float*>(&e)[i]))throw std::runtime_error("Non-finite GPU telegraph");
  std::vector<std::pair<float,int>> required;
  for(const auto& e:next){auto key=std::make_pair(e.halfAngle,std::max(1,int(ceilf(std::clamp(e.range,0.f,32.f)/.5f))));if(std::find(required.begin(),required.end(),key)==required.end())required.push_back(key);}
  if(required.size()>64)throw std::runtime_error("Too many distinct GPU effect templates");
  size_t missing=0;for(auto key:required)if(std::none_of(v.telegraphTemplates.begin(),v.telegraphTemplates.end(),[&](const auto& t){return t.halfAngle==key.first && t.rings==key.second;}))++missing;
  if(v.telegraphTemplates.size()+missing>64){v.renderer->flush();v.telegraphTemplates.clear();v.telegraphBytes=0;}
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
