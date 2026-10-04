#pragma once
#ifdef STARLIGHT_RR_STREAMLINE
#include "ray_reconstruction_streamline.h"
#else
#include <nvsdk_ngx.h>
#include <nvsdk_ngx_helpers_dlssd.h>
#include <mutex>

// NGX owns the feature ABI. The vendor feature DLL is not a public Evaluate export.
// Serialize NGX calls across independently owned editor viewport devices.
struct RayReconstruction {
 inline static std::mutex sdkMutex;
 HMODULE module=nullptr; ID3D12Device* device=nullptr;
 NVSDK_NGX_Parameter* parameters=nullptr; NVSDK_NGX_Handle* feature=nullptr;
 bool attempted=false,initialized=false,active=false,failed=false;
 std::string status="RR not requested";
 std::wstring directory;
 void failure(const char* stage,NVSDK_NGX_Result result){failed=true;active=false;status=std::string(stage)+" (NGX "+std::to_string((unsigned)result)+"); native denoiser";}
 bool create(ID3D12Device* dev,ID3D12GraphicsCommandList* cmd,UINT width,UINT height){
  if(attempted)return feature&&!failed;attempted=true;
  if(GetEnvironmentVariableW(L"STARLIGHT_DISABLE_RR",nullptr,0)){status="RR disabled; native denoiser";return false;}
  wchar_t path[32768]{};DWORD length=GetEnvironmentVariableW(L"STARLIGHT_RR_DIRECTORY",path,32768);
  if(length&&length<32768)directory=path;
  else {
   length=GetModuleFileNameW(nullptr,path,32768);
   if(!length||length>=32768){status="RR runtime location unavailable; native denoiser";return false;}
   directory.assign(path,length);auto slash=directory.find_last_of(L"\\/");
   if(slash==std::wstring::npos){status="RR runtime location invalid; native denoiser";return false;}
   directory.resize(slash);
  }
  module=LoadLibraryExW((directory+L"\\nvngx_dlssd.dll").c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
  if(!module){status="RR DLL unavailable; native denoiser (Win32 "+std::to_string(GetLastError())+")";return false;}
  std::lock_guard<std::mutex> lock(sdkMutex);device=dev;
  const wchar_t* paths[]={directory.c_str()};NVSDK_NGX_FeatureCommonInfo info{};info.PathListInfo={paths,1};
  auto result=NVSDK_NGX_D3D12_Init_with_ProjectID("79fbc6b8-9bc7-4d0c-a29d-7cd39bef2321",NVSDK_NGX_ENGINE_TYPE_CUSTOM,"Starlight-native-RR-1",directory.c_str(),dev,&info);
  if(NVSDK_NGX_FAILED(result)){failure("RR initialization failed",result);return false;}initialized=true;
  result=NVSDK_NGX_D3D12_GetCapabilityParameters(&parameters);if(NVSDK_NGX_FAILED(result)){failure("RR capability query failed",result);return false;}
  int available=0;parameters->Get(NVSDK_NGX_Parameter_SuperSamplingDenoising_Available,&available);
  if(!available){status="RR unsupported by current driver/device; native denoiser";return false;}
  NVSDK_NGX_DLSSD_Create_Params create{};create.InWidth=create.InTargetWidth=width;create.InHeight=create.InTargetHeight=height;
  create.InPerfQualityValue=NVSDK_NGX_PerfQuality_Value_DLAA;create.InDenoiseMode=NVSDK_NGX_DLSS_Denoise_Mode_DLUnified;
  create.InRoughnessMode=NVSDK_NGX_DLSS_Roughness_Mode_Packed;create.InUseHWDepth=NVSDK_NGX_DLSS_Depth_Type_Linear;
  create.InFeatureCreateFlags=NVSDK_NGX_DLSS_Feature_Flags_IsHDR;
  result=NGX_D3D12_CREATE_DLSSD_EXT(cmd,1,1,&feature,parameters,&create);
  if(NVSDK_NGX_FAILED(result)){failure("Native RR feature creation failed",result);return false;}status="Ray Reconstruction: native 1:1";return true;
 }
 bool evaluate(ID3D12GraphicsCommandList* cmd,ID3D12Resource* color,ID3D12Resource* output,ID3D12Resource* albedo,ID3D12Resource* specular,ID3D12Resource* normals,ID3D12Resource* depth,ID3D12Resource* motion,ID3D12Resource* specularDistance,UINT width,UINT height,bool reset,float milliseconds,XMFLOAT4X4& view,XMFLOAT4X4& projection){
  if(!feature||failed)return false;std::lock_guard<std::mutex> lock(sdkMutex);
  NVSDK_NGX_D3D12_DLSSD_Eval_Params p{};p.pInColor=color;p.pInOutput=output;p.pInDiffuseAlbedo=albedo;p.pInSpecularAlbedo=specular;p.pInNormals=normals;p.pInDepth=depth;p.pInMotionVectors=motion;p.pInSpecularHitDistance=specularDistance;
  p.InRenderSubrectDimensions={width,height};p.InReset=reset?1:0;p.InMVScaleX=p.InMVScaleY=1;p.InPreExposure=p.InExposureScale=1;p.InFrameTimeDeltaInMsec=milliseconds;
  p.pInWorldToViewMatrix=&view._11;p.pInViewToClipMatrix=&projection._11;
  auto result=NGX_D3D12_EVALUATE_DLSSD_EXT(cmd,feature,parameters,&p);if(NVSDK_NGX_FAILED(result)){failure("RR evaluation failed",result);return false;}active=true;status="Ray Reconstruction: native 1:1";return true;
 }
 // Caller retires GPU work before shutdown; never release an in-flight feature.
 void shutdown(){std::lock_guard<std::mutex> lock(sdkMutex);if(feature)NVSDK_NGX_D3D12_ReleaseFeature(feature);feature=nullptr;if(parameters)NVSDK_NGX_D3D12_DestroyParameters(parameters);parameters=nullptr;if(initialized)NVSDK_NGX_D3D12_Shutdown1(device);initialized=false;if(module)FreeLibrary(module);module=nullptr;active=false;}
 ~RayReconstruction(){shutdown();}
};
#endif
