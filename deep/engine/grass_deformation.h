#pragma once
#include "event_log.h"
#include "gpu_lifetime.h"
#include <grass_shader.h>
#include <algorithm>
#include <map>
#include <set>

// Immutable topology is shared; displacement/velocity and motion history belong
// to each placed instance. The render queue orders compute, shadows and color.
struct GrassDeformation {
 struct Body {XMFLOAT4 low,high;};
 struct Topology {ComPtr<ID3D12Resource> rest,adjacency;UINT vertices=0,triangleBase=0;XMFLOAT3 low{},high{};uint64_t allocationBytes=0;};
 // Deformed vertices and spring state of every instance live in shared pools, so one dispatch can serve all of them.
 struct Pool {ComPtr<ID3D12Resource> vb,state;UINT capacity=0,used=0;std::vector<std::pair<UINT,UINT>> freeRanges;bool readable=false;uint64_t allocationBytes=0;};
 struct Entry {uint64_t meshKey=0;std::shared_ptr<Topology> topology;GpuMesh mesh;Pool* pool=nullptr;UINT offset=0;bool fresh=true,readable=false;float shadowReach=.16f;uint64_t allocationBytes=0;};
 struct Constants {XMFLOAT4X4 world,inverse;XMFLOAT4 clock;UINT settings[4]{};};
 struct EntryRecord {XMFLOAT4X4 world,inverse;XMFLOAT4 clock;UINT settings[4];UINT range[4];};
 static_assert(sizeof(Constants)==160 && sizeof(Vertex)==56 && sizeof(Body)==32 && sizeof(EntryRecord)==176);
 static constexpr UINT kMaxEntryRecords=16384;
 std::map<uint64_t,Entry> entries;
 std::map<uint64_t,std::weak_ptr<Topology>> topologies;
 struct Pending {Entry* entry;Constants constants;};
 std::vector<Pending> pending;
 std::vector<std::unique_ptr<Pool>> pools;
 ComPtr<ID3D12Resource> entryRing;
 uint8_t* entryMapped=nullptr;
 UINT entryCursor=0;
 std::set<Entry*> pendingEntries;
 struct Retired {
  struct Range {Pool* pool;UINT offset,count;};
  uint64_t fence=0,bytes=0,items=0;std::vector<Range> ranges;std::vector<std::shared_ptr<Topology>> topologies;std::vector<ComPtr<ID3D12Resource>> buffers;
  void add(Entry& entry){
   if(entry.pool&&entry.topology){ranges.push_back({entry.pool,entry.offset,entry.topology->vertices});++items;}
   if(entry.topology&&std::find(topologies.begin(),topologies.end(),entry.topology)==topologies.end()){bytes+=entry.topology->allocationBytes;++items;topologies.push_back(entry.topology);}
  }
  void add(ComPtr<ID3D12Resource>& resource,uint64_t size){if(!resource)return;bytes+=size;++items;buffers.push_back(std::move(resource));}
 };
 GpuRetireQueue<Retired> retired{"ltGrass",64,1024ull<<20};Retired staged;
 static void give(Pool& pool,UINT offset,UINT count){
  auto& r=pool.freeRanges;r.push_back({offset,count});std::sort(r.begin(),r.end());
  size_t out=0;for(size_t i=1;i<r.size();++i){if(r[out].first+r[out].second==r[i].first)r[out].second+=r[i].second;else r[++out]=r[i];}
  r.resize(out+1);if(r.back().first+r.back().second==pool.used){pool.used=r.back().first;r.pop_back();}
 }
 static void release(Retired& batch){for(auto& range:batch.ranges)give(*range.pool,range.offset,range.count);}
 void commit(RenderDevice& d){if(!staged.items)return;Retired batch=std::move(staged);staged=Retired{};batch.fence=d.fenceVal;retired.push(d,std::move(batch),[](Retired& done){release(done);});}
 void drain(RenderDevice& d,bool all=false){commit(d);retired.drain(d,[](Retired& done){release(done);},all);}
 std::vector<Body> contacts;
 ComPtr<ID3D12RootSignature> root;
 ComPtr<ID3D12PipelineState> deform,normals;
 ComPtr<ID3D12Resource> bodyRing;
 uint8_t* bodyMapped=nullptr;
 float previousTime=0;bool hasTime=false;
 UINT bodyCapacity=0;
 uint64_t bodyAllocationBytes=0;
 uint64_t dispatches=0;
 static bool accepts(float kind){return kind>1.355f&&kind<1.365f;}
 static uint64_t allocation(RenderDevice& d,const ComPtr<ID3D12Resource>& resource){auto desc=starlight_resource_desc(resource);return d.device->GetResourceAllocationInfo(0,1,&desc).SizeInBytes;}
 static uint64_t identity(uint64_t motion,size_t index){return motion?motion:0x8000000000000000ull+index;}
 static ComPtr<ID3D12Resource> buffer(RenderDevice& d,UINT64 bytes,D3D12_HEAP_TYPE heapType,D3D12_RESOURCE_FLAGS flags,D3D12_RESOURCE_STATES initial){
  ComPtr<ID3D12Resource> resource;D3D12_HEAP_PROPERTIES heap{};heap.Type=heapType;D3D12_RESOURCE_DESC desc{};
  desc.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;desc.Width=std::max<UINT64>(256,bytes);desc.Height=1;desc.DepthOrArraySize=1;desc.MipLevels=1;desc.SampleDesc.Count=1;desc.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;desc.Flags=flags;
  auto created=heapType==D3D12_HEAP_TYPE_DEFAULT?D3D12_RESOURCE_STATE_COMMON:initial;
  d.must(d.device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&desc,created,nullptr,IID_PPV_ARGS(&resource)),"grass buffer");
  if(created!=initial)d.barrier(resource.Get(),created,initial);return resource;
 }
 void init(RenderDevice& d){
  if(root)return;
  D3D12_ROOT_PARAMETER parameters[7]{};
  parameters[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;parameters[0].Constants={0,0,4};
  for(UINT i=1;i<4;++i){parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[i].Descriptor={i-1,0};}
  for(UINT i=4;i<6;++i){parameters[i].ParameterType=D3D12_ROOT_PARAMETER_TYPE_UAV;parameters[i].Descriptor={i-4,0};}
  parameters[6].ParameterType=D3D12_ROOT_PARAMETER_TYPE_SRV;parameters[6].Descriptor={3,0};
  D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=7;desc.pParameters=parameters;
  ComPtr<ID3DBlob> signature,error;d.must(D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,&signature,&error),"grass root serialization");
  d.must(d.device->CreateRootSignature(0,signature->GetBufferPointer(),signature->GetBufferSize(),IID_PPV_ARGS(&root)),"grass root");
  auto pipeline=[&](const char* entry,ComPtr<ID3D12PipelineState>& out){ComPtr<ID3DBlob> shader,errors;auto hr=D3DCompile(kGrassShader,strlen(kGrassShader),"grass_deformation",nullptr,nullptr,entry,"cs_5_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&shader,&errors);if(FAILED(hr))throw std::runtime_error(errors?std::string((char*)errors->GetBufferPointer(),errors->GetBufferSize()):"grass shader compile failed");D3D12_COMPUTE_PIPELINE_STATE_DESC p{};p.pRootSignature=root.Get();p.CS={shader->GetBufferPointer(),shader->GetBufferSize()};d.must(d.device->CreateComputePipelineState(&p,IID_PPV_ARGS(&out)),"grass compute pipeline");};
  pipeline("Deform",deform);pipeline("Normals",normals);
  entryRing=buffer(d,UINT64(kMaxEntryRecords)*sizeof(EntryRecord)*kFrameCount,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ);
  D3D12_RANGE none{0,0};d.must(entryRing->Map(0,&none,reinterpret_cast<void**>(&entryMapped)),"grass entry table map");
 }
 // First fit in a pool that has room (free ranges first, then the unused tail); a new pool when none does.
 void place(RenderDevice& d,Entry& entry,UINT vertices){
  for(auto& pool:pools){
   for(auto it=pool->freeRanges.begin();it!=pool->freeRanges.end();++it)if(it->second>=vertices){entry.pool=pool.get();entry.offset=it->first;if(it->second==vertices)pool->freeRanges.erase(it);else{it->first+=vertices;it->second-=vertices;}return;}
   if(pool->capacity-pool->used>=vertices){entry.pool=pool.get();entry.offset=pool->used;pool->used+=vertices;return;}
  }
  auto pool=std::make_unique<Pool>();pool->capacity=std::max<UINT>(vertices,1u<<20);
  pool->vb=buffer(d,UINT64(pool->capacity)*sizeof(Vertex),D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  pool->state=buffer(d,UINT64(pool->capacity)*32,D3D12_HEAP_TYPE_DEFAULT,D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
  pool->allocationBytes=allocation(d,pool->vb)+allocation(d,pool->state);
  entry.pool=pool.get();entry.offset=0;pool->used=vertices;pools.push_back(std::move(pool));
 }
 uint64_t poolBytes()const{uint64_t total=0;for(const auto& pool:pools)total+=pool->allocationBytes;return total+(entryRing?UINT64(kMaxEntryRecords)*sizeof(EntryRecord)*kFrameCount:0);}
 // Called at a scene-upload fence, with the upload command list open.
 void prepare(RenderDevice& d,uint64_t key,uint64_t meshKey,const Mesh& source,const GpuMesh& gpu){
  if(auto found=entries.find(key);found!=entries.end()&&found->second.meshKey==meshKey)return;
  init(d);std::shared_ptr<Topology> topology=topologies[meshKey].lock();
  if(!topology){
   topology=std::make_shared<Topology>();topology->vertices=UINT(source.verts.size());
   topology->low={1e20f,1e20f,1e20f};topology->high={-1e20f,-1e20f,-1e20f};for(const auto& v:source.verts){auto& low=topology->low;auto& high=topology->high;low.x=std::min(low.x,v.px);low.y=std::min(low.y,v.py);low.z=std::min(low.z,v.pz);high.x=std::max(high.x,v.px);high.y=std::max(high.y,v.py);high.z=std::max(high.z,v.pz);}
   std::vector<std::vector<UINT>> faces(source.verts.size());for(UINT i=0;i<source.indices.size();i+=3)for(UINT k=0;k<3;++k)faces[source.indices[i+k]].push_back(i/3);
   std::vector<UINT> data(source.verts.size()+1);UINT offset=0;for(size_t i=0;i<faces.size();++i){data[i]=offset;offset+=UINT(faces[i].size());}data[faces.size()]=offset;
   for(const auto& face:faces)data.insert(data.end(),face.begin(),face.end());topology->triangleBase=UINT(data.size());data.insert(data.end(),source.indices.begin(),source.indices.end());
   topology->rest=d.createDefaultBuffer(source.verts.data(),source.verts.size()*sizeof(Vertex),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
   topology->adjacency=d.createDefaultBuffer(data.data(),data.size()*sizeof(UINT),D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);topology->allocationBytes=allocation(d,topology->rest)+allocation(d,topology->adjacency);topologies[meshKey]=topology;
  }
  Entry entry;entry.meshKey=meshKey;entry.topology=topology;entry.mesh=gpu;
  place(d,entry,UINT(source.verts.size()));
  entry.mesh.vb=entry.pool->vb;
  entry.mesh.vbv.BufferLocation=entry.pool->vb->GetGPUVirtualAddress()+UINT64(entry.offset)*sizeof(Vertex);
  if(auto replaced=entries.find(key);replaced!=entries.end()){staged.add(replaced->second);replaced->second=std::move(entry);}else entries[key]=std::move(entry);
 }
 void reset(){hasTime=false;for(auto& pair:entries){pair.second.fresh=true;pair.second.shadowReach=.16f;}}
 void bodies(RenderDevice& d,const std::vector<Body>& next){
  if(!bodyRing||next.size()>bodyCapacity){if(bodyRing){bodyRing->Unmap(0,nullptr);bodyMapped=nullptr;Retired old;old.add(bodyRing,bodyAllocationBytes);old.fence=d.fenceVal;retired.push(d,std::move(old),[](Retired& done){release(done);});}bodyCapacity=UINT(std::max<size_t>(1,next.size()));bodyRing=buffer(d,UINT64(bodyCapacity)*sizeof(Body)*kFrameCount,D3D12_HEAP_TYPE_UPLOAD,D3D12_RESOURCE_FLAG_NONE,D3D12_RESOURCE_STATE_GENERIC_READ);bodyAllocationBytes=allocation(d,bodyRing);D3D12_RANGE none{0,0};d.must(bodyRing->Map(0,&none,reinterpret_cast<void**>(&bodyMapped)),"map grass contacts");}
  contacts=next;
 }
 void begin(RenderDevice& d,float time){
  entryCursor=0;
  if(entries.empty())return;
  if(!contacts.empty())memcpy(bodyMapped+size_t(d.slot)*bodyCapacity*sizeof(Body),contacts.data(),contacts.size()*sizeof(Body));
  if(hasTime&&time<previousTime)reset();
 }

 void flush(RenderDevice& d){
  if(pending.empty())return;
  struct Group {Pool* pool;Topology* topology;std::vector<size_t> items;};
  std::vector<Group> groups;std::vector<Pool*> involved;
  for(size_t i=0;i<pending.size();++i){
   auto& entry=*pending[i].entry;Group* group=nullptr;
   for(auto& g:groups)if(g.pool==entry.pool&&g.topology==entry.topology.get()){group=&g;break;}
   if(!group){groups.push_back({entry.pool,entry.topology.get(),{}});group=&groups.back();}
   group->items.push_back(i);
   if(std::find(involved.begin(),involved.end(),entry.pool)==involved.end())involved.push_back(entry.pool);
  }
  std::vector<D3D12_RESOURCE_BARRIER> barriers;
  for(Pool* pool:involved){
   if(pool->readable)d.barrier(pool->vb.Get(),D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER,D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
   D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=pool->state.Get();barriers.push_back(barrier);
  }
  d.cmd->ResourceBarrier(UINT(barriers.size()),barriers.data());
  d.cmd->SetComputeRootSignature(root.Get());
  constexpr UINT kMaxGroups=60000;
  for(auto& group:groups){
   auto& topology=*group.topology;const UINT groupsPerEntry=(topology.vertices+63)/64;
   for(size_t begin=0;begin<group.items.size();){
    const size_t most=std::max<size_t>(1,kMaxGroups/std::max<UINT>(1,groupsPerEntry));
    const size_t end=std::min(group.items.size(),begin+most);const UINT count=UINT(end-begin);
    if(entryCursor+count>kMaxEntryRecords)throw std::runtime_error("Grass instances exceed the per-frame table capacity");
    const UINT64 tableOffset=(UINT64(d.slot)*kMaxEntryRecords+entryCursor)*sizeof(EntryRecord);
    auto* records=reinterpret_cast<EntryRecord*>(entryMapped+tableOffset);UINT firstGroup=0;
    for(size_t k=begin;k<end;++k){
     const auto& p=pending[group.items[k]];EntryRecord& r=records[k-begin];
     r.world=p.constants.world;r.inverse=p.constants.inverse;r.clock=p.constants.clock;
     for(int w=0;w<4;++w)r.settings[w]=p.constants.settings[w];
     r.range[0]=p.entry->offset;r.range[1]=topology.vertices;r.range[2]=firstGroup;r.range[3]=0;firstGroup+=groupsPerEntry;
    }
    entryCursor+=count;
    const UINT info[4]{count,0,0,0};
    d.cmd->SetComputeRoot32BitConstants(0,4,info,0);
    d.cmd->SetComputeRootShaderResourceView(1,topology.rest->GetGPUVirtualAddress());
    d.cmd->SetComputeRootShaderResourceView(2,bodyRing->GetGPUVirtualAddress()+UINT64(d.slot)*bodyCapacity*sizeof(Body));
    d.cmd->SetComputeRootShaderResourceView(3,topology.adjacency->GetGPUVirtualAddress());
    d.cmd->SetComputeRootUnorderedAccessView(4,group.pool->vb->GetGPUVirtualAddress());
    d.cmd->SetComputeRootUnorderedAccessView(5,group.pool->state->GetGPUVirtualAddress());
    d.cmd->SetComputeRootShaderResourceView(6,entryRing->GetGPUVirtualAddress()+tableOffset);
    d.cmd->SetPipelineState(deform.Get());d.cmd->Dispatch(firstGroup,1,1);
    D3D12_RESOURCE_BARRIER barrier{};barrier.Type=D3D12_RESOURCE_BARRIER_TYPE_UAV;barrier.UAV.pResource=group.pool->vb.Get();d.cmd->ResourceBarrier(1,&barrier);
    d.cmd->SetPipelineState(normals.Get());d.cmd->Dispatch(firstGroup,1,1);
    begin=end;
   }
  }
  for(Pool* pool:involved){d.barrier(pool->vb.Get(),D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER);pool->readable=true;}
  for(const auto& p:pending){p.entry->readable=true;++dispatches;}
  pending.clear();pendingEntries.clear();
 }
 const GpuMesh& draw(RenderDevice& d,uint64_t key,const XMMATRIX& world,float time){
  auto& entry=entries.at(key);auto& topology=*entry.topology;
  // Repeated keys retain the original sequential dependency, even if a caller
  // intentionally uses the same state resource more than once in a frame.
  if(pendingEntries.count(&entry))flush(d);
  Constants c{};XMStoreFloat4x4(&c.world,world);XMStoreFloat4x4(&c.inverse,XMMatrixInverse(nullptr,world));
  c.clock={time,hasTime?std::max(0.f,time-previousTime):0.f,float(topology.vertices),float(contacts.size())};c.settings[0]=entry.fresh;c.settings[1]=topology.triangleBase;
  pending.push_back({&entry,c});pendingEntries.insert(&entry);entry.fresh=false;return entry.mesh;
 }
 void end(RenderDevice& d,float time){flush(d);previousTime=time;hasTime=true;}
 float reach(uint64_t key,const XMMATRIX& world){
  const auto& mesh=*entries.at(key).topology;XMFLOAT3 low{1e20f,1e20f,1e20f},high{-1e20f,-1e20f,-1e20f};
  for(UINT k=0;k<8;++k){XMFLOAT3 p;XMStoreFloat3(&p,XMVector3TransformCoord(XMVectorSet(k&1?mesh.high.x:mesh.low.x,k&2?mesh.high.y:mesh.low.y,k&4?mesh.high.z:mesh.low.z,1),world));low.x=std::min(low.x,p.x);low.y=std::min(low.y,p.y);low.z=std::min(low.z,p.z);high.x=std::max(high.x,p.x);high.y=std::max(high.y,p.y);high.z=std::max(high.z,p.z);}
  UINT count=0;for(const auto& b:contacts)if(low.x<=b.high.x+.2f&&high.x>=b.low.x-.2f&&low.z<=b.high.z+.2f&&high.z>=b.low.z-.2f&&low.y<=b.high.y+.1f&&high.y>=b.low.y-.1f)++count;
  // Returning the global count would inflate cascades for irrelevant distant
  // bodies. Keep the conservative bound tied to actual possible contacts.
  auto& entry=entries.at(key);entry.shadowReach=std::max(entry.shadowReach,.16f+.8f*count);return entry.shadowReach;
 }
 void retain(RenderDevice& d,const std::set<uint64_t>& active){for(auto it=entries.begin();it!=entries.end();){if(!active.count(it->first)){staged.add(it->second);it=entries.erase(it);}else++it;}commit(d);for(auto it=topologies.begin();it!=topologies.end();)if(it->second.expired())it=topologies.erase(it);else++it;}
};
