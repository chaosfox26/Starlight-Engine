#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include <d3d12.h>
#include <d3d12sdklayers.h>
#include <wrl/client.h>

// Carry a matching runtime for diagnostics. Availability of the debug layer
// is measured separately; app-local DLLs alone do not prove it can initialize.
extern "C" {
__declspec(dllexport) extern const UINT D3D12SDKVersion = 619;
__declspec(dllexport) const char* D3D12SDKPath = ".\\D3D12\\";
}

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <fstream>
#include <filesystem>
#include <shellapi.h>
#include <string>
#include <sstream>
#include <limits>
#include <vector>
extern "C" __declspec(dllimport) void *sl_create(HWND, int, int);
extern "C" __declspec(dllimport) int sl_scene(void *, const unsigned char *,
                                              int);
extern "C" __declspec(dllimport) int sl_frame(void *, double, float);
extern "C" __declspec(dllimport) void sl_camera(void *, float, float, float,
                                                float, float, float);
extern "C" __declspec(dllimport) void sl_destroy(void *);
extern "C" __declspec(dllimport) int sl_surface_weather(void*,float,float,float,float,const float*,int);
extern "C" __declspec(dllimport) void sl_shot(void *, const wchar_t *);
extern "C" __declspec(dllimport) double sl_gpu_ms(void *);
extern "C" __declspec(dllimport) const char *sl_error();
extern "C" __declspec(dllimport) int sl_render_width(void *);
extern "C" __declspec(dllimport) void sl_rt(void *, int, int);
extern "C" __declspec(dllimport) void sl_freeze(void *, int);
extern "C" __declspec(dllimport) int sl_rt_supported(void *);
extern "C" __declspec(dllimport) int sl_rt_active(void *);
extern "C" __declspec(dllimport) int sl_rt_samples(void *);
extern "C" __declspec(dllimport) double sl_ray_ms(void *);
extern "C" __declspec(dllimport) double sl_as_ms(void *);
extern "C" __declspec(dllimport) unsigned long long sl_blas_builds(void *);
extern "C" __declspec(dllimport) unsigned long long sl_tlas_refits(void *);
extern "C" __declspec(dllimport) double sl_denoise_ms(void *);
extern "C" __declspec(dllimport) int sl_rr_active(void *);
extern "C" __declspec(dllimport) const char *sl_rr_status(void *);
extern "C" __declspec(dllimport) void sl_denoiser(void *, int);
extern "C" __declspec(dllimport) int sl_fog_channels(void*,double*);
extern "C" __declspec(dllimport) int sl_fog_sources(void*,const float*,int);
extern "C" __declspec(dllimport) int sl_fog_source_types(void*,const float*,int);
extern "C" __declspec(dllimport) int sl_effect_clock(void*,double);
extern "C" __declspec(dllimport) float sl_effect_time(void*);
extern "C" __declspec(dllimport) void sl_environment_reset(void*);
extern "C" __declspec(dllimport) void sl_surface_texture_stats(void *,uint64_t *);
extern "C" __declspec(dllimport) int sl_local_shadow_stats(void *,uint64_t *);
extern "C" __declspec(dllimport) int sl_shadow_settings(void *,int);
extern "C" __declspec(dllimport) int sl_environment_lights(void *,const float*,int);
extern "C" __declspec(dllimport) int sl_sun_shadow_stats(void *,uint64_t *);
extern "C" __declspec(dllimport) int sl_hdr_post_stats(void *,uint64_t *);
extern "C" __declspec(dllimport) int sl_hdr_post_settings(void *,int,float);
extern "C" __declspec(dllimport) int sl_surface_texture_tail(void *,uint8_t *);
extern "C" __declspec(dllimport) uint64_t sl_water_scene_bytes(void *);
extern "C" __declspec(dllimport) int sl_resize(void *,int,int);
extern "C" __declspec(dllimport) int sl_grass_contacts(void*,const float*,int);
extern "C" __declspec(dllimport) int sl_grass_readback(void*,int,float*,int);
extern "C" __declspec(dllimport) int sl_grass_reach(void*,int,float*);
extern "C" __declspec(dllimport) int sl_grass_stats(void*,uint64_t*);
extern "C" __declspec(dllimport) int sl_motion_readback(void*,double*);

#include "../../deep/src/json.hpp"
#include "../../deep/engine/surface_texture_data.h"
#include "../audit/visible_resolution.hpp"
using json = nlohmann::json;
// Assertion failures in the VALIDATE_* blocks use this type so they exit 15 (a real defect), never 11 (an environment precondition).
struct ValidationFailure : std::runtime_error { using std::runtime_error::runtime_error; };
#include "grass_gpu_checks.inl"
#include "night_sky_checks.inl"
static bool interactive = false, userStarted = false, userClosed = false;
static LRESULT CALLBACK Proc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
  if (interactive && (msg == WM_LBUTTONUP || (msg == WM_KEYDOWN && w == VK_SPACE))) {
    userStarted = true;
    return 0;
  }
  if (msg == WM_CLOSE) {
    userClosed = true;
    PostQuitMessage(0);
    return 0;
  }
  return DefWindowProc(hwnd, msg, w, l);
}
int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int) {
  int argc = 0;
  auto argv = CommandLineToArgvW(GetCommandLineW(), &argc);
  if(argc==3 && std::wstring(argv[1])==L"--graphics-environment") {
    Microsoft::WRL::ComPtr<ID3D12Debug> debug;
    const auto hr=D3D12GetDebugInterface(IID_PPV_ARGS(&debug));
    json evidence={{"debugInterfaceAvailable",SUCCEEDED(hr)},{"debugInterfaceHresult",static_cast<uint32_t>(hr)},
                   {"requestedSdkVersion",D3D12SDKVersion},{"deviceCreated",false},{"modules",json::array()}};
    Microsoft::WRL::ComPtr<ID3D12SDKConfiguration1> configuration;
    const auto getInterface=reinterpret_cast<decltype(&D3D12GetInterface)>(GetProcAddress(GetModuleHandleW(L"d3d12.dll"),"D3D12GetInterface"));
    const auto configurationResult=getInterface?getInterface(CLSID_D3D12SDKConfiguration,IID_PPV_ARGS(&configuration)):HRESULT_FROM_WIN32(ERROR_PROC_NOT_FOUND);
    evidence["factoryConfigurationHresult"]=static_cast<uint32_t>(configurationResult);
    if(SUCCEEDED(configurationResult)) {
      Microsoft::WRL::ComPtr<ID3D12DeviceFactory> factory;
      const auto factoryResult=configuration->CreateDeviceFactory(D3D12SDKVersion,".\\D3D12\\",IID_PPV_ARGS(&factory));
      evidence["factoryCreationHresult"]=static_cast<uint32_t>(factoryResult);
      if(SUCCEEDED(factoryResult)) {
        Microsoft::WRL::ComPtr<ID3D12Debug> factoryDebug;
        const auto factoryDebugResult=factory->GetConfigurationInterface(CLSID_D3D12Debug,IID_PPV_ARGS(&factoryDebug));
        evidence["factoryDebugHresult"]=static_cast<uint32_t>(factoryDebugResult);
        evidence["factoryDebugAvailable"]=SUCCEEDED(factoryDebugResult);
      }
    }
    for(const auto* name:{L"D3D12.dll",L"D3D12Core.dll",L"d3d12SDKLayers.dll"}) {
      wchar_t path[32768]{};const auto module=GetModuleHandleW(name);
      if(module && GetModuleFileNameW(module,path,32768)) {
        const auto value=std::filesystem::path(path).generic_u8string();
        evidence["modules"].push_back(std::string(value.begin(),value.end()));
      }
    }
    std::ofstream output{std::filesystem::path(argv[2])};output<<evidence.dump(2);output.close();
    LocalFree(argv);return output?0:2;
  }
  if (argc < 6 || argc > 10) {
    LocalFree(argv);
    return 2;
  }
  std::wstring packet = argv[1], report = argv[2], shot = argv[3],
               mode = argc > 7 ? argv[7] : L"focused";
  int width = _wtoi(argv[4]), height = _wtoi(argv[5]),
      tier = argc > 6 ? _wtoi(argv[6]) : 0,
      bounces = argc > 8 ? _wtoi(argv[8]) : 3,
      denoiser = argc > 9 ? _wtoi(argv[9]) : 0;
  LocalFree(argv);
  interactive = mode == L"interactive";
  const bool fullscreen = mode == L"fullscreen-auto";
  SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
  wchar_t durationSetting[16]{};
  GetEnvironmentVariableW(L"STARLIGHT_BENCH_SECONDS", durationSetting, 16);
  const double measurementSeconds = durationSetting[0] ? std::clamp(_wtoi(durationSetting), 1, 60) : 5;
  const bool animated=GetEnvironmentVariableW(L"STARLIGHT_BENCH_ANIMATE",nullptr,0)>0;
  auto dll = GetModuleHandleW(L"Starlight.Viewport.dll");
  auto benchmark = reinterpret_cast<void (*)(void *, int)>(
      GetProcAddress(dll, "sl_benchmark"));
  auto drain = reinterpret_cast<void (*)(void *)>(
      GetProcAddress(dll, "sl_benchmark_flush"));
  auto contract = reinterpret_cast<int (*)()>(GetProcAddress(dll, "sl_benchmark_contract"));
  const int benchmarkContract = contract ? contract() : 0;
  using QueryInt = int (*)(void*);
  auto renderHeight = reinterpret_cast<QueryInt>(GetProcAddress(dll, "sl_render_height"));
  auto debugActive = reinterpret_cast<QueryInt>(GetProcAddress(dll, "sl_debug_active"));
  auto nativeContract = reinterpret_cast<QueryInt>(GetProcAddress(dll, "sl_native_contract"));
  auto gpuMemory=reinterpret_cast<int (*)(void*,uint64_t*)>(GetProcAddress(dll,"sl_gpu_memory"));
  bool requested = mode == L"--disable-background-throttle",
       offscreen = requested && benchmark && drain && benchmarkContract >= 2;
  // The immutable legacy DLL has no offscreen ABI. Run it focused and label the
  // different presentation contract, rather than pretending it was uncapped.
  bool headless = offscreen || mode == L"headless";
  HWND hwnd = nullptr;
  void *context = nullptr;
  auto finish = [&](json data, int code) {
    data["success"] = code == 0;
    data["exitCode"] = code;
    data["userStarted"] = userStarted;
    data["interactive"] = interactive;
    data["fullscreen"] = fullscreen;
    data["automaticStart"] = !interactive;
    if (code == 9 || code == 10 || code == 11) data["failureCategory"] = "environment-precondition";
    if (hwnd) {
      DWORD foregroundPid=0;
      GetWindowThreadProcessId(GetForegroundWindow(),&foregroundPid);
      LASTINPUTINFO input{sizeof(input)}; GetLastInputInfo(&input);
      data["windowEvidence"]={{"visible",IsWindowVisible(hwnd)!=0},{"minimized",IsIconic(hwnd)!=0},
        {"foregroundOwnerPid",foregroundPid},{"testPid",GetCurrentProcessId()},
        {"lastInputAgeMs",DWORD(GetTickCount()-input.dwTime)}};
    }
    std::ofstream f{std::filesystem::path(report)};
    f << data.dump(2);
    f.close();
    if (interactive && hwnd && !userClosed) {
      auto title = code == 0 ? L"Starlight Gate 0 — COMPLETED; report saved. Close this window when ready."
                            : L"Starlight Gate 0 — TEST FAILED; report saved. Close this window when ready.";
      SetWindowTextW(hwnd, title);
      MSG message{};
      while (GetMessageW(&message, nullptr, 0, 0) > 0) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
      }
    }
    if (context)
      sl_destroy(context);
    if (hwnd)
      DestroyWindow(hwnd);
    return code;
  };
  try {
    std::ifstream input(std::filesystem::path(packet), std::ios::binary);
    std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),
                                     {});
    if (bytes.empty())
      return finish({{"error", "Empty packet"}}, 3);
    WNDCLASSW wc{};
    wc.hInstance = instance;
    wc.lpfnWndProc = Proc;
    wc.lpszClassName = L"StarlightStandaloneCheck";
    RegisterClassW(&wc);
    RECT rect{0, 0, width, height};
    const bool bounded = mode == L"bounded-auto";
    const DWORD style = (fullscreen || bounded) ? WS_POPUP : WS_OVERLAPPEDWINDOW;
    const auto resolution = starlight::audit::visible_resolution(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN));
    if ((fullscreen || bounded) && (!resolution.supported || width != resolution.width || height != resolution.height || fullscreen != resolution.fullscreen))
      return finish({{"error", "Visible audit requires at least 1280x720 and preserves aspect ratio within a 4K pixel budget"}}, 11);
    AdjustWindowRect(&rect, style, FALSE);
    hwnd =
        CreateWindowW(wc.lpszClassName, L"Starlight — Gate 0 visible rendering check",
                      style, 0, 0, rect.right, rect.bottom,
                      nullptr, nullptr, instance, nullptr);
    if (!hwnd)
      return finish({{"error", "Window creation failed"}}, 4);
    if (!headless) {
      ShowWindow(hwnd, SW_SHOW);
      // A launcher may supply SW_HIDE for the first ShowWindow call.
      // The explicitly visible test must not inherit that hidden state.
      ShowWindow(hwnd, SW_SHOW);
      HWND previous = GetForegroundWindow();
      DWORD other = previous ? GetWindowThreadProcessId(previous, nullptr) : 0;
      DWORD current = GetCurrentThreadId();
      bool attached = other && other != current && AttachThreadInput(current, other, TRUE);
      BringWindowToTop(hwnd);
      SetForegroundWindow(hwnd);
      SetActiveWindow(hwnd);
      SetFocus(hwnd);
      if (attached) AttachThreadInput(current, other, FALSE);
    }
    context = sl_create(hwnd, width, height);
    if (!context)
      return finish({{"error", sl_error()}}, 4);
    if (offscreen)
      benchmark(context, 1);
    if (!sl_scene(context, bytes.data(), int(bytes.size())))
      return finish({{"error", sl_error()}}, 5);
    std::array<float,6> camera={0,9.36f,13,0,0,0};wchar_t cameraSetting[256]{};
    if(GetEnvironmentVariableW(L"STARLIGHT_BENCH_CAMERA",cameraSetting,256)){
      wchar_t* cursor=cameraSetting;for(auto& value:camera){wchar_t* end=nullptr;value=wcstof(cursor,&end);if(end==cursor||!std::isfinite(value))throw std::runtime_error("Invalid benchmark camera");cursor=end;}
    }
    sl_camera(context,camera[0],camera[1],camera[2],camera[3],camera[4],camera[5]);
    sl_rt(context, tier, bounces);
    wchar_t effectTimeSetting[32]{};if(GetEnvironmentVariableW(L"STARLIGHT_BENCH_EFFECT_TIME",effectTimeSetting,32)){wchar_t* end=nullptr;double time=wcstod(effectTimeSetting,&end);if(end==effectTimeSetting||!sl_effect_clock(context,time))throw std::runtime_error("Invalid benchmark effect time");}
    wchar_t weatherSetting[256]{};
    if(GetEnvironmentVariableW(L"STARLIGHT_BENCH_SURFACE_WEATHER",weatherSetting,256)){
      std::wistringstream input(weatherSetting);float moisture,rain,windX,windZ;std::wstring extra;
      if(!(input>>moisture>>rain>>windX>>windZ)||(input>>extra))throw std::runtime_error("Invalid benchmark surface weather");
      std::array<float,6> shelter{};int shelterCount=0;wchar_t shelterSetting[256]{};
      if(GetEnvironmentVariableW(L"STARLIGHT_BENCH_WEATHER_SHELTER",shelterSetting,256)){std::wistringstream bounds(shelterSetting);for(auto& value:shelter)if(!(bounds>>value))throw std::runtime_error("Invalid benchmark shelter");if(bounds>>extra)throw std::runtime_error("Invalid benchmark shelter suffix");shelterCount=1;}
      if(!sl_surface_weather(context,moisture,rain,windX,windZ,shelter.data(),shelterCount))throw std::runtime_error(sl_error());
    }
    sl_freeze(context, animated?0:1);
    json grassEvidence,nightSkyEvidence;
    if(GetEnvironmentVariableW(L"STARLIGHT_VALIDATE_NIGHT_SKY",nullptr,0)){
      if(tier||width!=2560||height!=1440)throw std::runtime_error("Night sky contract requires native 1440p raster");
      nightSkyEvidence=night_sky_checks(context,std::filesystem::path(report).parent_path()/"night-sky");
      if(!sl_scene(context,bytes.data(),int(bytes.size())))throw std::runtime_error(sl_error());
      sl_camera(context,camera[0],camera[1],camera[2],camera[3],camera[4],camera[5]);
      if(!sl_surface_weather(context,0,0,0,0,nullptr,0))throw std::runtime_error(sl_error());
    }
    if(GetEnvironmentVariableW(L"STARLIGHT_VALIDATE_GRASS_CONTACTS",nullptr,0)){
      if(tier)throw std::runtime_error("Grass contact validation requires native raster");
      // The check needs the dedicated one-instance grass fixture; any other packet (verify_all passes an ordinary scene) is swapped for the built-in one, then restored.
      uint32_t magic=0;if(bytes.size()>=4)memcpy(&magic,bytes.data(),4);std::vector<unsigned char> grassPacket;
      if(magic!=0x34564c53){grassPacket=grass_fixture_packet();if(!sl_scene(context,grassPacket.data(),int(grassPacket.size())))throw std::runtime_error(sl_error());}
      sl_freeze(context,0);grassEvidence=grass_gpu_checks(context,grassPacket.empty()?bytes:grassPacket);sl_freeze(context,animated?0:1);
      if(!grassPacket.empty()){if(!sl_scene(context,bytes.data(),int(bytes.size())))throw std::runtime_error(sl_error());sl_camera(context,camera[0],camera[1],camera[2],camera[3],camera[4],camera[5]);}
    }
    // The soot and chaos probes read the fog field, so the scene needs the emitter each one is about; verify_all's ordinary scene has none. Soot is a fire emitter (type 0), chaos a cloud emitter (type 1).
    {const bool soot=GetEnvironmentVariableW(L"STARLIGHT_VALIDATE_SOOT",nullptr,0)>0,chaos=GetEnvironmentVariableW(L"STARLIGHT_VALIDATE_FOG",nullptr,0)>0;
     if(soot&&chaos)throw std::runtime_error("STARLIGHT_VALIDATE_SOOT and STARLIGHT_VALIDATE_FOG need separate runs: each is about a different kind of emitter");
     if(soot||chaos){const float source[4]{4,.5f,-3,1.5f},type=soot?0.f:1.f;if(!sl_fog_sources(context,source,1)||!sl_fog_source_types(context,&type,1))throw std::runtime_error(sl_error());}}
    wchar_t grassBodySetting[256]{};
    if(GetEnvironmentVariableW(L"STARLIGHT_BENCH_GRASS_BODY",grassBodySetting,256)){
      std::array<float,6> box{};std::wistringstream input(grassBodySetting);std::wstring extra;for(auto& value:box)if(!(input>>value))throw std::runtime_error("Invalid grass showcase contact");if(input>>extra||!sl_grass_contacts(context,box.data(),1))throw std::runtime_error("Invalid grass showcase contact");
    }
    sl_denoiser(context, denoiser);
    bool localLifecycleValidated=GetEnvironmentVariableW(L"STARLIGHT_VALIDATE_LOCAL_SHADOW_LIFECYCLE",nullptr,0)>0;
    if(localLifecycleValidated){
      if(tier)throw std::runtime_error("Local shadow lifecycle probe requires raster rendering");
      std::array<float,32*12> lamps{};
      for(int i=0;i<32;++i){auto p=lamps.data()+i*12;p[0]=(i%8-3.5f)*2;p[1]=3;p[2]=(i/8-1.5f)*2;p[3]=6;p[5]=-1;p[7]=-2;p[8]=p[9]=p[10]=1;p[11]=-1;}
      uint64_t held[6]{};
      for(int count:{1,32,31,8,1,0,32,0,1}){
        if(!sl_environment_lights(context,count?lamps.data():nullptr,count)||!sl_frame(context,0,13))throw std::runtime_error(sl_error());
        uint64_t state[6]{};sl_local_shadow_stats(context,state);
        if(state[2]<uint64_t(count)||(count&&state[3]==0)||(!count&&(state[2]!=held[2]||state[1]!=held[1]||state[3]!=0||state[2]==0)))throw ValidationFailure("Changing light counts did not retain and release shadow resources correctly: count="+std::to_string(count)+" capacity="+std::to_string(state[2])+" bytes="+std::to_string(state[1])+" draws="+std::to_string(state[3]));
        std::copy(state,state+6,held);
      }
      // An empty scene keeps its array through a short gap (a flashing light must not flush the GPU), then frees it.
      for(int frame=0;frame<300;++frame){
        if(!sl_environment_lights(context,nullptr,0)||!sl_frame(context,0,13))throw std::runtime_error(sl_error());
        uint64_t state[6]{};sl_local_shadow_stats(context,state);
        if(frame<200&&(state[2]!=held[2]||state[1]!=held[1]))throw ValidationFailure("Empty scene released local shadow resources before the delay: frame="+std::to_string(frame));
        if(frame==299&&(state[2]!=0||state[1]!=65536))throw ValidationFailure("Empty scene never released local shadow resources: capacity="+std::to_string(state[2])+" bytes="+std::to_string(state[1]));
      }
      uint64_t before[6]{},after[6]{};sl_local_shadow_stats(context,before);
      if(sl_environment_lights(context,lamps.data(),33))throw ValidationFailure("Invalid local light count accepted");
      if(!sl_frame(context,0,13))throw std::runtime_error(sl_error());sl_local_shadow_stats(context,after);
      if(!std::equal(before,before+6,after))throw ValidationFailure("Invalid light update changed active shadows");
      if(!sl_scene(context,bytes.data(),int(bytes.size())))throw std::runtime_error(sl_error());
    }
    if(GetEnvironmentVariableW(L"STARLIGHT_VALIDATE_SHADOW_RESOLUTION_OVERRIDE",nullptr,0)>0){
      wchar_t requested[32]{};GetEnvironmentVariableW(L"STARLIGHT_SHADOW_RESOLUTION",requested,32);
      if(!requested[0])throw std::runtime_error("STARLIGHT_VALIDATE_SHADOW_RESOLUTION_OVERRIDE needs STARLIGHT_SHADOW_RESOLUTION set");
      if(GetEnvironmentVariableW(L"STARLIGHT_SUN_SHADOWS",nullptr,0)>0)throw std::runtime_error("STARLIGHT_VALIDATE_SHADOW_RESOLUTION_OVERRIDE needs STARLIGHT_SUN_SHADOWS unset: a set value pins sun shadows, so turning them off would not be tested");
      if(!sl_shadow_settings(context,0)||!sl_frame(context,0,13))throw std::runtime_error(sl_error());
      if(!sl_shadow_settings(context,2)||!sl_frame(context,0,13))throw std::runtime_error(sl_error());
      uint64_t sun[4]{};sl_sun_shadow_stats(context,sun);
      if(sun[0]!=uint64_t(_wtoi(requested))||sun[3]!=1)throw ValidationFailure("Shadow resolution override was lost after shadows were turned off and on: resolution="+std::to_string(sun[0])+" enabled="+std::to_string(sun[3]));
    }
    bool shadowSettingsValidated=GetEnvironmentVariableW(L"STARLIGHT_VALIDATE_SHADOW_SETTINGS",nullptr,0)>0;
    if(shadowSettingsValidated){
      uint64_t sunBytes[4]{},localBytes[4]{};
      for(int quality:{1,2,3,0}){
        if(!sl_shadow_settings(context,quality)||!sl_frame(context,0,13))throw std::runtime_error(sl_error());
        uint64_t sun[4]{},local[6]{};sl_sun_shadow_stats(context,sun);sl_local_shadow_stats(context,local);sunBytes[quality]=sun[1];localBytes[quality]=local[1];
        if(sun[0]!=uint64_t(!quality?1:quality==1?1024:quality==3?4096:2048)||local[0]!=uint64_t(quality==1?256:quality==3?1024:512)||sun[3]!=uint64_t(quality>0)||local[5]!=uint64_t(quality>0))throw ValidationFailure("Shadow quality did not set expected resources");
      }
      if(!(sunBytes[0]<sunBytes[1]&&sunBytes[1]<sunBytes[2]&&sunBytes[2]<sunBytes[3]&&localBytes[0]<localBytes[1]&&localBytes[1]<localBytes[2]&&localBytes[2]<localBytes[3]))throw ValidationFailure("Shadow presets did not scale memory");
      uint64_t before[6]{},after[6]{};sl_local_shadow_stats(context,before);
      if(sl_shadow_settings(context,-1)||sl_shadow_settings(context,4))throw ValidationFailure("Invalid shadow quality accepted");
      sl_local_shadow_stats(context,after);if(!std::equal(before,before+6,after))throw ValidationFailure("Invalid shadow quality changed resources");
      if(!sl_shadow_settings(context,2))throw std::runtime_error(sl_error());
    }
    bool glowSettingsValidated=GetEnvironmentVariableW(L"STARLIGHT_VALIDATE_GLOW_SETTINGS",nullptr,0)>0;
    if(glowSettingsValidated){
      uint64_t original[4]{};sl_hdr_post_stats(context,original);uint64_t bytes[4]{};
      for(int quality=0;quality<4;++quality){
        if(!sl_hdr_post_settings(context,quality,.12f)||!sl_frame(context,0,13))return finish({{"error",sl_error()}},15);
        uint64_t state[4]{};sl_hdr_post_stats(context,state);bytes[quality]=state[1];
        if(state[0]!=uint64_t(quality==0?0:quality==1?3:quality==2?5:6)||state[3]!=uint64_t(quality?state[0]*2:1))return finish({{"error","Glow preset did not recreate the expected resources"}},15);
      }
      uint64_t before[4]{},after[4]{};sl_hdr_post_stats(context,before);
      if(sl_hdr_post_settings(context,4,.12f)||sl_hdr_post_settings(context,2,std::numeric_limits<float>::quiet_NaN()))return finish({{"error","Invalid glow settings accepted"}},15);
      sl_hdr_post_stats(context,after);if(!std::equal(before,before+4,after))return finish({{"error","Invalid glow settings altered resource state"}},15);
      if(!(bytes[0]<bytes[1]&&bytes[1]<bytes[2]&&bytes[2]<bytes[3]))return finish({{"error","Glow presets did not scale their allocations"}},15);
      int restore=original[0]==0?0:original[0]==3?1:original[0]==6?3:2;
      if(!sl_hdr_post_settings(context,restore,.12f))return finish({{"error",sl_error()}},15);
    }
    // Image comparisons must use the same temporal sample sequence, rather
    // than whichever frame happens to land after a wall-clock warmup.
    uint64_t captureFrame=0,submittedFrames=0,capturedFrame=0;
    wchar_t captureFrameSetting[32]{};
    if(GetEnvironmentVariableW(L"STARLIGHT_BENCH_CAPTURE_FRAME",captureFrameSetting,32)){
      wchar_t* end=nullptr;auto value=wcstoul(captureFrameSetting,&end,10);
      if(!headless||animated||end==captureFrameSetting||*end||value<1||value>4096)
        throw std::runtime_error("Fixed capture frame requires a frozen headless test and a frame from 1 to 4096");
      captureFrame=value;
    }
    bool resizeValidated=GetEnvironmentVariableW(L"STARLIGHT_VALIDATE_RESIZE",nullptr,0)>0;
    if(captureFrame&&resizeValidated)throw std::runtime_error("Fixed capture frame cannot be combined with the resize probe");
    if(resizeValidated){if(!sl_frame(context,0,13)||!sl_resize(context,std::max(128,width*2/3),std::max(128,height*2/3)))return finish({{"error",sl_error()}},13);for(int i=0;i<3;++i)if(!sl_frame(context,0,13))return finish({{"error",sl_error()}},13);if(!sl_resize(context,width,height))return finish({{"error",sl_error()}},13);for(int i=0;i<3;++i)if(!sl_frame(context,0,13))return finish({{"error",sl_error()}},13);}
    if (interactive)
      SetWindowTextW(hwnd, L"Starlight Gate 0 — READY: click the scene or press Space to start the visible test");
    using Clock = std::chrono::steady_clock;
    auto start = Clock::now(), measure = start, focusSince = start;
    bool measuring = false, captured = false;
    long frames = 0, foregroundFrames = 0;
    std::vector<double> times;
    double gpu = 0, ray = 0, as = 0, noise = 0;
    uint64_t localMemoryPeak=0,localMemoryBudget=0,privateMemoryPeak=0,workingSetPeak=0;bool memoryAvailable=false;
    MSG msg{};
    while (true) {
      while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
        if (msg.message == WM_QUIT)
          return finish({{"error", "Benchmark closed"}}, 6);
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
      }
      auto now = Clock::now();
      // Interactive verification does not start or time out before the user
      // has actually seen and selected this window.
      if (interactive && !userStarted) { start = now; focusSince = now; }
      if (std::chrono::duration<double>(now - start).count() > measurementSeconds + 40)
        return finish({{"error", "Stable foreground focus unavailable"}}, 9);
      if (!headless && GetForegroundWindow() != hwnd) {
        if (measuring)
          return finish(
              {{"error", "Focus lost during measured presentation"}},
              10);
        focusSince = now;
      }
      double warm = std::chrono::duration<double>(now - focusSince).count();
      if (!captured && (captureFrame ? submittedFrames+1==captureFrame : warm>1)) {
        sl_shot(context, shot.c_str());
        captured = true;
      }
      if (warm > 2 && !measuring) {
        if (drain)
          drain(context);
        measure = Clock::now();
        measuring = true;
      }
      if (measuring &&
          std::chrono::duration<double>(now - measure).count() >= measurementSeconds &&
          (!captureFrame || capturedFrame==captureFrame))
        break;
      auto frameStart = Clock::now();
      if (!sl_frame(context, 0, 13))
        return finish({{"error", sl_error()}}, 7);
      ++submittedFrames;
      if(captured&&!capturedFrame)capturedFrame=submittedFrames;
      if (measuring) {
        ++frames;
        if(frames%32==1){
          uint64_t memory[2]{};if(gpuMemory&&gpuMemory(context,memory)){memoryAvailable=true;localMemoryPeak=std::max(localMemoryPeak,memory[0]);localMemoryBudget=memory[1];}
          PROCESS_MEMORY_COUNTERS_EX cpu{};cpu.cb=sizeof(cpu);if(GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&cpu),sizeof(cpu))){privateMemoryPeak=std::max(privateMemoryPeak,uint64_t(cpu.PrivateUsage));workingSetPeak=std::max(workingSetPeak,uint64_t(cpu.WorkingSetSize));}
        }
        if (GetForegroundWindow() == hwnd)
          ++foregroundFrames;
        times.push_back(
            std::chrono::duration<double, std::milli>(Clock::now() - frameStart)
                .count());
        gpu += sl_gpu_ms(context);
        ray += sl_ray_ms(context);
        as += sl_as_ms(context);
        noise += sl_denoise_ms(context);
        if (!headless && frames % 60 == 0) {
          auto seconds = std::chrono::duration<double>(Clock::now() - measure).count();
          auto title = L"Starlight Gate 0 — live native rendering | " + std::to_wstring(width) + L"x" + std::to_wstring(height) +
            L" | " + std::to_wstring(static_cast<int>(seconds)) + L"/" + std::to_wstring(static_cast<int>(measurementSeconds)) +
            L" sec | GPU " + std::to_wstring(sl_gpu_ms(context)) + L" ms | frames " + std::to_wstring(frames);
          SetWindowTextW(hwnd, title.c_str());
        }
      }
    }
    // Include completion of all submitted GPU work in throughput, never just
    // CPU dispatch.
    if (drain)
      drain(context);
    double duration =
        std::chrono::duration<double>(Clock::now() - measure).count();
    std::sort(times.begin(), times.end());
    auto percentile = [&](double p) {
      return times.empty()
                 ? 0
                 : times[std::min(times.size() - 1, size_t(p * times.size()))];
    };
    if(animated){sl_shot(context,(shot+L".motion.png").c_str());if(!sl_frame(context,0,13))return finish({{"error",sl_error()}},7);if(drain)drain(context);}
    uint64_t surfaceStats[4]{};sl_surface_texture_stats(context,surfaceStats);
    uint64_t localShadowStats[6]{};if(!sl_local_shadow_stats(context,localShadowStats))throw std::runtime_error("Local shadow stats unavailable");
    uint64_t shadowStats[4]{};if(!sl_sun_shadow_stats(context,shadowStats))throw std::runtime_error("Shadow stats unavailable");
    uint64_t postStats[4]{};if(!sl_hdr_post_stats(context,postStats))return finish({{"error","HDR post statistics unavailable"}},15);
    bool surfaceValidated=GetEnvironmentVariableW(L"STARLIGHT_VALIDATE_SURFACE_TEXTURES",nullptr,0)>0;
    if(surfaceValidated){std::array<uint8_t,60> actual{};if(!sl_surface_texture_tail(context,actual.data()))return finish({{"error",sl_error()}},12);for(int material=0;material<5;++material){auto expected=starlight::surface::builtin(UINT(surfaceStats[0]),material);for(int channel=0;channel<3;++channel)if(!std::equal(expected.back().maps[channel].begin(),expected.back().maps[channel].end(),actual.data()+(channel*5+material)*4))return finish({{"error","Surface texture array mip readback mismatch"}},12);}}
    json fogEvidence=nullptr;double capturedEffectTime=sl_effect_time(context);
    if(GetEnvironmentVariableW(L"STARLIGHT_VALIDATE_SOOT",nullptr,0)){
      std::array<double,8> channels{};if(!sl_fog_channels(context,channels.data()))return finish({{"error",sl_error()}},14);
      if(channels[1]<=1||channels[2]>.001||channels[3]>.001)return finish({{"error","Ordinary smoke leaked into chaos or solid cells"},{"fog",channels}},14);
      fogEvidence={{"channels",channels},{"sootSeparationVerified",true}};
    }
    if(GetEnvironmentVariableW(L"STARLIGHT_VALIDATE_FOG",nullptr,0)){
      std::array<double,8> initial{},removed{},reset{};if(!sl_fog_channels(context,initial.data()))return finish({{"error",sl_error()}},14);
      if(initial[2]<=1||initial[1]>.001||initial[3]>.001)return finish({{"error","Chaos field leaked into soot or solid cells"},{"fog",initial}},14);
      if(!sl_fog_sources(context,nullptr,0)||!sl_fog_source_types(context,nullptr,0))return finish({{"error",sl_error()}},14);
      double begin=sl_effect_time(context);sl_freeze(context,0);
      for(int i=1;i<=120;++i){if(!sl_effect_clock(context,begin+i/60.)||!sl_frame(context,0,13))return finish({{"error",sl_error()}},14);}
      if(!sl_fog_channels(context,removed.data()))return finish({{"error",sl_error()}},14);
      if(removed[2]<=0||removed[2]>=initial[2]||removed[3]>.001||removed[4]<=initial[4]+.1)return finish({{"error","Detached chaos fog did not drift, decay or avoid solid cells"},{"initial",initial},{"removed",removed}},14);
      sl_shot(context,(shot+L".detached.png").c_str());sl_environment_reset(context);if(!sl_frame(context,0,13)||!sl_fog_channels(context,reset.data()))return finish({{"error",sl_error()}},14);
      if(reset[2]>.001)return finish({{"error","Chaos field survived environment reset"},{"reset",reset}},14);
      fogEvidence={{"initial",initial},{"afterRemoval",removed},{"afterReset",reset},{"transportAndSeparationVerified",true}};
    }
    return finish({{"fps", frames / duration},
                   {"requestedCaptureFrame",captureFrame},
                   {"capturedFrame",capturedFrame},
                   {"fogEvidence",fogEvidence},
                   {"capturedEffectTime",capturedEffectTime},
                   {"surfaceTextureResolution",surfaceStats[0]},
                   {"surfaceTextureMipLevels",surfaceStats[1]},
                   {"surfaceTextureAllocationBytes",surfaceStats[2]},
                   {"surfaceTextureMaterialCount",surfaceStats[3]},
                   {"grassContactChecks",grassEvidence},
                   {"nightSkyChecks",nightSkyEvidence},
                   {"surfaceTextureTailVerified",surfaceValidated},
                   {"localShadowResolution",localShadowStats[0]},
                   {"localShadowAllocationBytes",localShadowStats[1]},
                   {"localShadowCapacity",localShadowStats[2]},
                   {"localShadowDraws",localShadowStats[3]},
                   {"localShadowCulledDraws",localShadowStats[4]},
                   {"localShadowsEnabled",localShadowStats[5]!=0},
                   {"localShadowLifecycleVerified",localLifecycleValidated},
                   {"shadowSettingsVerified",shadowSettingsValidated},
                   {"sunShadowResolution",shadowStats[0]},
                   {"sunShadowAllocationBytes",shadowStats[1]},
                   {"sunShadowDraws",shadowStats[2]},
                   {"sunShadowsEnabled",shadowStats[3]!=0},
                   {"bloomEnabled",postStats[2]!=0},
                   {"bloomLevels",postStats[0]},
                   {"hdrPostAllocationBytes",postStats[1]},
                   {"hdrPostPasses",postStats[3]},
                   {"glowSettingsVerified",glowSettingsValidated},
                   {"resizeResourcesVerified",resizeValidated},
                   {"waterSceneGuideAllocationBytes",sl_water_scene_bytes(context)},
                   {"sampledPeakLocalVideoMemoryBytes",memoryAvailable?json(localMemoryPeak):json(nullptr)},
                   {"localVideoMemoryBudgetBytes",memoryAvailable?json(localMemoryBudget):json(nullptr)},
                   {"sampledPeakPrivateMemoryBytes",privateMemoryPeak},
                   {"sampledPeakWorkingSetBytes",workingSetPeak},
                   {"camera",camera},
                   {"frames", frames},
                   {"durationSeconds", duration},
                   {"width", width},
                   {"height", height},
                   {"renderWidth", sl_render_width(context)},
                   {"renderHeight", renderHeight ? json(renderHeight(context)) : json(nullptr)},
                   {"nativeContractVerified", nativeContract ? json(nativeContract(context) != 0) : json(nullptr)},
                   {"debugLayerActive", debugActive ? json(debugActive(context) != 0) : json(nullptr)},
                   {"gpuMs", gpu / std::max(1L, frames)},
                   {"p50Ms", percentile(.5)},
                   {"p95Ms", percentile(.95)},
                   {"p99Ms", percentile(.99)},
                   {"upscaling", false},
                   {"frameGeneration", false},
                   {"foreground", GetForegroundWindow() == hwnd},
                   {"foregroundFrames", foregroundFrames},
                   {"headless", headless},
                   {"backgroundThrottleBypassed", offscreen},
                   {"benchmarkContract", benchmarkContract},
                   {"debugLayerRequested", GetEnvironmentVariableW(L"STARLIGHT_D3D_DEBUG", nullptr, 0) != 0},
                   {"presentationMode", offscreen ? "offscreen-no-present"
                                                  : "focused-present"},
                   {"legacyFrameCap", offscreen ? 0 : 500},
                   {"requestedTier", tier},
                   {"activeTier", sl_rt_active(context)},
                   {"rtSupported", sl_rt_supported(context) != 0},
                   {"rayDispatchMs", ray / std::max(1L, frames)},
                   {"tlasMs", as / std::max(1L, frames)},
                   {"denoiseMs", noise / std::max(1L, frames)},
                   {"blasBuilds", sl_blas_builds(context)},
                   {"tlasRefits", sl_tlas_refits(context)},
                   {"samples", sl_rt_samples(context)},
                   {"bounces", bounces},
                   {"rrActive", sl_rr_active(context) != 0},
                   {"rrStatus", sl_rr_status(context)},
                   {"denoiserRequested", denoiser},
                   {"denoiserName",
                    denoiser == 0 ? "native-wavelet" : "ray-reconstruction"},
                   {"frozenSnapshot", !animated}},
                  0);
  } catch (const ValidationFailure &e) {
    return finish({{"error", e.what()}}, 15);
  } catch (const std::exception &e) {
    return finish({{"error", e.what()}}, 11);
  }
}
