#pragma once
#include <sl.h>
#include <sl_dlss_d.h>
#include <sl_consts.h>
#include <array>
#include <mutex>
#include <cmath>

struct StarlightStreamline {
 std::mutex mutex;
 HMODULE module=nullptr;
 unsigned clients=0,nextViewport=1,frame=0;
 bool initialized=false;
 Microsoft::WRL::ComPtr<ID3D12Device> device;
 std::wstring directory;
 std::string status="RR not initialized";
 PFun_slInit* init=nullptr;
 PFun_slShutdown* shutdown=nullptr;
 PFun_slSetD3DDevice* setDevice=nullptr;
 PFun_slIsFeatureSupported* supported=nullptr;
 PFun_slGetFeatureFunction* featureFunction=nullptr;
 PFun_slGetNewFrameToken* newFrame=nullptr;
 PFun_slSetConstants* constants=nullptr;
 PFun_slSetTagForFrame* tags=nullptr;
 PFun_slEvaluateFeature* evaluate=nullptr;
 PFun_slFreeResources* freeResources=nullptr;
 PFun_slDLSSDSetOptions* options=nullptr;
 template<class T>bool load(T*& function,const char* name){function=reinterpret_cast<T*>(GetProcAddress(module,name));return function!=nullptr;}
 bool result(sl::Result value,const char* stage){if(value==sl::Result::eOk)return true;status=std::string(stage)+" (Streamline "+std::to_string(unsigned(value))+"); native denoiser";return false;}
 void start(){
  if(GetEnvironmentVariableW(L"STARLIGHT_DISABLE_RR",nullptr,0)){status="RR disabled; native denoiser";return;}
  wchar_t path[32768]{};DWORD n=GetEnvironmentVariableW(L"STARLIGHT_RR_DIRECTORY",path,32768);
  if(n&&n<32768)directory.assign(path,n);
  else {n=GetModuleFileNameW(nullptr,path,32768);if(!n||n>=32768){status="RR runtime location unavailable; native denoiser";return;}directory.assign(path,n);auto slash=directory.find_last_of(L"\\/");if(slash==std::wstring::npos){status="RR runtime location invalid; native denoiser";return;}directory.resize(slash);}
  module=LoadLibraryExW((directory+L"\\sl.interposer.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if(!module){status="RR Streamline DLL unavailable; native denoiser (Win32 "+std::to_string(GetLastError())+")";return;}
  if(!load(init,"slInit")||!load(shutdown,"slShutdown")||!load(setDevice,"slSetD3DDevice")||!load(supported,"slIsFeatureSupported")||!load(featureFunction,"slGetFeatureFunction")||!load(newFrame,"slGetNewFrameToken")||!load(constants,"slSetConstants")||!load(tags,"slSetTagForFrame")||!load(evaluate,"slEvaluateFeature")||!load(freeResources,"slFreeResources")){status="RR Streamline API incomplete; native denoiser";return;}
  const wchar_t* paths[]={directory.c_str()};sl::Feature features[]={sl::kFeatureDLSS_RR};sl::Preferences p{};
  p.pathsToPlugins=paths;p.numPathsToPlugins=1;p.pathToLogsAndData=directory.c_str();p.featuresToLoad=features;p.numFeaturesToLoad=1;p.renderAPI=sl::RenderAPI::eD3D12;
  p.flags=sl::PreferenceFlags::eDisableCLStateTracking|sl::PreferenceFlags::eUseManualHooking|sl::PreferenceFlags::eUseFrameBasedResourceTagging;
  p.engine=sl::EngineType::eCustom;p.engineVersion="Starlight-native-RR-1";p.projectId="79fbc6b8-9bc7-4d0c-a29d-7cd39bef2321";
  initialized=result(init(p,sl::kSDKVersion),"RR initialization failed");
 }
 bool bind(ID3D12Device* candidate){
  if(!initialized)return false;
  auto luid=adapterLuid(candidate);
  sl::AdapterInfo adapter{};adapter.deviceLUID=reinterpret_cast<uint8_t*>(&luid);adapter.deviceLUIDSizeInBytes=sizeof(luid);
  if(!result(supported(sl::kFeatureDLSS_RR,adapter),"RR unsupported by current driver/device"))return false;
  if(device&&device.Get()!=candidate){status="RR Streamline requires one shared D3D12 device; native denoiser";return false;}
  if(!device){if(!result(setDevice(candidate),"RR device binding failed"))return false;device=candidate;}
  if(!options){void* value=nullptr;if(!result(featureFunction(sl::kFeatureDLSS_RR,"slDLSSDSetOptions",value),"RR options API unavailable")||!value)return false;options=reinterpret_cast<PFun_slDLSSDSetOptions*>(value);}
  return true;
 }
 template<class Device>static LUID adapterLuid(Device* device){LUID value{};if constexpr(requires { device->GetAdapterLuid(&value); }){device->GetAdapterLuid(&value);return value;}else return device->GetAdapterLuid();}
 void stop(){if(initialized)shutdown();initialized=false;device.Reset();options=nullptr;if(module)FreeLibrary(module);module=nullptr;frame=0;nextViewport=1;}
};

struct RayReconstruction {
 inline static StarlightStreamline runtime;
 sl::ViewportHandle viewport;
 bool attempted=false,initialized=false,active=false,failed=false,hasPrevious=false;
 std::string status="RR not requested";
 DirectX::XMFLOAT4X4 previousVP{};
 RayReconstruction(){std::lock_guard<std::mutex> lock(runtime.mutex);if(runtime.clients++==0)runtime.start();viewport=sl::ViewportHandle{runtime.nextViewport++};}
 RayReconstruction(const RayReconstruction&)=delete;
 RayReconstruction& operator=(const RayReconstruction&)=delete;
 static sl::float4x4 matrix(DirectX::CXMMATRIX m){DirectX::XMFLOAT4X4 f;DirectX::XMStoreFloat4x4(&f,m);sl::float4x4 out;for(int r=0;r<4;r++)out[r]={f.m[r][0],f.m[r][1],f.m[r][2],f.m[r][3]};return out;}
 bool result(sl::Result value,const char* stage){if(value==sl::Result::eOk)return true;failed=true;active=false;status=std::string(stage)+" (Streamline "+std::to_string(unsigned(value))+"); native denoiser";return false;}
 bool create(ID3D12Device* device,ID3D12GraphicsCommandList*,UINT,UINT){
  if(attempted)return initialized&&!failed;attempted=true;std::lock_guard<std::mutex> lock(runtime.mutex);
  if(!runtime.bind(device)){status=runtime.status;return false;}initialized=true;return true;
 }
 bool evaluate(ID3D12GraphicsCommandList* command,ID3D12Resource* color,ID3D12Resource* output,ID3D12Resource* albedo,ID3D12Resource* specular,ID3D12Resource* normals,ID3D12Resource* depth,ID3D12Resource* motion,ID3D12Resource* specularDistance,UINT width,UINT height,bool reset,float,DirectX::XMFLOAT4X4& view,DirectX::XMFLOAT4X4& projection){
  if(!initialized||failed)return false;std::lock_guard<std::mutex> lock(runtime.mutex);
  using namespace DirectX;auto v=XMLoadFloat4x4(&view),p=XMLoadFloat4x4(&projection);auto invView=XMMatrixInverse(nullptr,v),vp=v*p;auto previous=reset||!hasPrevious?vp:XMLoadFloat4x4(&previousVP);auto toPrevious=XMMatrixInverse(nullptr,vp)*previous;
  sl::DLSSDOptions options{};options.mode=sl::DLSSMode::eDLAA;options.outputWidth=width;options.outputHeight=height;options.normalRoughnessMode=sl::DLSSDNormalRoughnessMode::ePacked;options.worldToCameraView=matrix(v);options.cameraViewToWorld=matrix(invView);
  if(!result(runtime.options(viewport,options),"RR options failed"))return false;
  sl::FrameToken* token=nullptr;auto index=runtime.frame++;if(!result(runtime.newFrame(token,&index),"RR frame token failed"))return false;if(!token){failed=true;status="RR frame token unavailable; native denoiser";return false;}
  sl::Constants c{};c.cameraViewToClip=matrix(p);c.clipToCameraView=matrix(XMMatrixInverse(nullptr,p));c.clipToPrevClip=matrix(toPrevious);c.prevClipToClip=matrix(XMMatrixInverse(nullptr,toPrevious));c.jitterOffset={0,0};c.mvecScale={1.f/width,1.f/height};c.cameraPinholeOffset={0,0};
  XMFLOAT4X4 world;XMStoreFloat4x4(&world,invView);c.cameraPos={world._41,world._42,world._43};c.cameraRight={world._11,world._12,world._13};c.cameraUp={world._21,world._22,world._23};c.cameraFwd={-world._31,-world._32,-world._33};
  c.cameraNear=projection._43/projection._33;c.cameraFar=projection._43/(projection._33+1);c.cameraFOV=2*std::atan(1.f/projection._22);c.cameraAspectRatio=projection._22/projection._11;c.depthInverted=sl::Boolean::eFalse;c.cameraMotionIncluded=sl::Boolean::eTrue;c.motionVectors3D=sl::Boolean::eFalse;c.reset=reset||!hasPrevious?sl::Boolean::eTrue:sl::Boolean::eFalse;
  if(!result(runtime.constants(c,*token,viewport),"RR camera constants failed"))return false;
  ID3D12Resource* input[]={color,output,albedo,specular,normals,depth,motion,specularDistance};sl::BufferType types[]={sl::kBufferTypeScalingInputColor,sl::kBufferTypeScalingOutputColor,sl::kBufferTypeAlbedo,sl::kBufferTypeSpecularAlbedo,sl::kBufferTypeNormalRoughness,sl::kBufferTypeLinearDepth,sl::kBufferTypeMotionVectors,sl::kBufferTypeSpecularHitDistance};
  sl::Extent extent{0,0,width,height};std::array<sl::Resource,8> resources;std::array<sl::ResourceTag,8> tags;
  for(unsigned i=0;i<8;i++){resources[i]=sl::Resource(sl::ResourceType::eTex2d,input[i],uint32_t(i==1?D3D12_RESOURCE_STATE_UNORDERED_ACCESS:D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE));tags[i]=sl::ResourceTag(&resources[i],types[i],sl::ResourceLifecycle::eOnlyValidNow,&extent);}
  if(!result(runtime.tags(*token,viewport,tags.data(),unsigned(tags.size()),command),"RR input tagging failed"))return false;
  const sl::BaseStructure* inputs[]={&viewport};if(!result(runtime.evaluate(sl::kFeatureDLSS_RR,*token,inputs,1,command),"RR evaluation failed"))return false;
  XMStoreFloat4x4(&previousVP,vp);hasPrevious=true;active=true;status="Ray Reconstruction: native 1:1";return true;
 }
 void shutdown(){std::lock_guard<std::mutex> lock(runtime.mutex);if(initialized)result(runtime.freeResources(sl::kFeatureDLSS_RR,viewport),"RR resource release failed");initialized=false;active=false;hasPrevious=false;}
 ~RayReconstruction(){shutdown();std::lock_guard<std::mutex> lock(runtime.mutex);if(--runtime.clients==0)runtime.stop();}
};
