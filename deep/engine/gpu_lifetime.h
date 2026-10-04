#pragma once
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <utility>
#include <vector>
#include "event_log.h"

struct GpuRetireStats {
 uint64_t pending=0,pendingItems=0,pendingBytes=0;
 uint64_t pushed=0,drained=0,drainedItems=0;
 uint64_t maxPending=0,maxPendingBytes=0;
 uint64_t forcedStalls=0,forcedStallMicros=0;
};

template<class Batch>
struct GpuRetireQueue {
 const char* tag;size_t capacity;uint64_t capacityBytes;
 std::deque<Batch> batches;GpuRetireStats stats;
 GpuRetireQueue(const char* name,size_t maxBatches,uint64_t maxBytes):tag(name),capacity(maxBatches),capacityBytes(maxBytes){}
 void note(){
  stats.pending=batches.size();
  stats.maxPending=std::max<uint64_t>(stats.maxPending,stats.pending);
  stats.maxPendingBytes=std::max(stats.maxPendingBytes,stats.pendingBytes);
 }
 uint64_t oldestFence()const{uint64_t oldest=UINT64_MAX;for(const auto& batch:batches)oldest=std::min(oldest,batch.fence);return oldest;}
 uint64_t newestFence()const{uint64_t newest=0;for(const auto& batch:batches)newest=std::max(newest,batch.fence);return newest;}
 template<class Device,class Release>
 size_t drain(Device& d,Release&& release,bool all=false){
  if(batches.empty())return 0;
  if(all)d.waitFence(newestFence());
  const uint64_t completed=d.fence->GetCompletedValue();
  size_t released=0,items=0;
  for(size_t i=0;i<batches.size();){
   if(batches[i].fence>completed){++i;continue;}
   Batch batch=std::move(batches[i]);batches.erase(batches.begin()+i);
   stats.pendingItems-=batch.items;stats.pendingBytes-=batch.bytes;items+=size_t(batch.items);
   release(batch);++released;
  }
  stats.pending=batches.size();
  if(released){stats.drained+=released;stats.drainedItems+=items;starlight_diag::evlog(tag,double(stats.pending),double(released),double(stats.forcedStalls));}
  return released;
 }
 template<class Device,class Release>
 void push(Device& d,Batch&& batch,Release&& release){
  ++stats.pushed;stats.pendingItems+=batch.items;stats.pendingBytes+=batch.bytes;batches.push_back(std::move(batch));
  note();
  while(batches.size()>capacity||stats.pendingBytes>capacityBytes){
   const double started=starlight_diag::evnow();
   d.waitFence(oldestFence());
   const double micros=(starlight_diag::evnow()-started)*1000.;
   ++stats.forcedStalls;stats.forcedStallMicros+=uint64_t(micros>0?micros:0);
   starlight_diag::evlog(tag,double(batches.size()),-1.,micros/1000.);
   drain(d,release);
  }
 }
};

struct GpuCommandRing {
 struct Slot {ComPtr<ID3D12CommandAllocator> allocator;UINT64 fence=0;};
 std::vector<Slot> slots;size_t current=0,limit;uint64_t stalls=0;
 explicit GpuCommandRing(size_t maxSlots=4):limit(maxSlots){}
 template<class Device>
 ID3D12CommandAllocator* acquire(Device& d,const char* tag){
  const UINT64 completed=d.fence->GetCompletedValue();
  for(size_t i=0;i<slots.size();++i)if(slots[i].fence<=completed){current=i;return slots[i].allocator.Get();}
  if(slots.size()<limit){
   Slot made;d.must(d.device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&made.allocator)),"create command allocator");
   slots.push_back(std::move(made));current=slots.size()-1;return slots[current].allocator.Get();
  }
  size_t oldest=0;for(size_t i=1;i<slots.size();++i)if(slots[i].fence<slots[oldest].fence)oldest=i;
  const double started=starlight_diag::evnow();
  d.waitFence(slots[oldest].fence);
  ++stalls;starlight_diag::evlog(tag,double(slots.size()),-2.,starlight_diag::evnow()-started);
  current=oldest;return slots[current].allocator.Get();
 }
 void stamp(UINT64 value){if(current<slots.size())slots[current].fence=value;}
};
