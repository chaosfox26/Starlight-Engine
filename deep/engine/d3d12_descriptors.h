#pragma once
#include <d3d12.h>
inline constexpr D3D12_HEAP_TYPE starlightGpuUploadHeap=static_cast<D3D12_HEAP_TYPE>(5);

namespace starlight_d3d12 {
template<class Heap>auto cpu(const Heap& heap,int) -> decltype(heap->GetCPUDescriptorHandleForHeapStart()) {return heap->GetCPUDescriptorHandleForHeapStart();}
template<class Heap>D3D12_CPU_DESCRIPTOR_HANDLE cpu(const Heap& heap,long) {D3D12_CPU_DESCRIPTOR_HANDLE value{};heap->GetCPUDescriptorHandleForHeapStart(&value);return value;}
template<class Heap>auto gpu(const Heap& heap,int) -> decltype(heap->GetGPUDescriptorHandleForHeapStart()) {return heap->GetGPUDescriptorHandleForHeapStart();}
template<class Heap>D3D12_GPU_DESCRIPTOR_HANDLE gpu(const Heap& heap,long) {D3D12_GPU_DESCRIPTOR_HANDLE value{};heap->GetGPUDescriptorHandleForHeapStart(&value);return value;}
template<class Resource>auto desc(const Resource& resource,int) -> decltype(resource->GetDesc()) {return resource->GetDesc();}
template<class Resource>D3D12_RESOURCE_DESC desc(const Resource& resource,long) {D3D12_RESOURCE_DESC value{};resource->GetDesc(&value);return value;}
template<class Heap>auto heapDesc(const Heap& heap,int) -> decltype(heap->GetDesc()) {return heap->GetDesc();}
template<class Heap>D3D12_DESCRIPTOR_HEAP_DESC heapDesc(const Heap& heap,long) {D3D12_DESCRIPTOR_HEAP_DESC value{};heap->GetDesc(&value);return value;}
}
template<class Heap>D3D12_CPU_DESCRIPTOR_HANDLE starlight_cpu_heap_start(const Heap& heap) {return starlight_d3d12::cpu(heap,0);}
template<class Heap>D3D12_GPU_DESCRIPTOR_HANDLE starlight_gpu_heap_start(const Heap& heap) {return starlight_d3d12::gpu(heap,0);}
template<class Resource>D3D12_RESOURCE_DESC starlight_resource_desc(const Resource& resource) {return starlight_d3d12::desc(resource,0);}
template<class Heap>D3D12_DESCRIPTOR_HEAP_DESC starlight_heap_desc(const Heap& heap) {return starlight_d3d12::heapDesc(heap,0);}
