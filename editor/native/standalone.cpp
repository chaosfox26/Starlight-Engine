#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <fstream>
#include <vector>
#include <chrono>
#include <string>
#include <algorithm>
extern "C" __declspec(dllimport) void* sl_create(HWND,int,int);
extern "C" __declspec(dllimport) int sl_scene(void*,const unsigned char*,int);
extern "C" __declspec(dllimport) int sl_frame(void*,double,float);
extern "C" __declspec(dllimport) void sl_camera(void*,float,float,float,float,float,float);
extern "C" __declspec(dllimport) void sl_destroy(void*);
extern "C" __declspec(dllimport) void sl_shot(void*,const wchar_t*);
extern "C" __declspec(dllimport) double sl_gpu_ms(void*);
extern "C" __declspec(dllimport) const char* sl_error();
extern "C" __declspec(dllimport) int sl_render_width(void*);
extern "C" __declspec(dllimport) void sl_rt(void*,int,int);
extern "C" __declspec(dllimport) void sl_freeze(void*,int);
extern "C" __declspec(dllimport) int sl_rt_supported(void*);
extern "C" __declspec(dllimport) int sl_rt_active(void*);
extern "C" __declspec(dllimport) int sl_rt_samples(void*);
extern "C" __declspec(dllimport) double sl_ray_ms(void*);
extern "C" __declspec(dllimport) double sl_as_ms(void*);
extern "C" __declspec(dllimport) unsigned long long sl_blas_builds(void*);
extern "C" __declspec(dllimport) unsigned long long sl_tlas_refits(void*);
extern "C" __declspec(dllimport) double sl_denoise_ms(void*);
extern "C" __declspec(dllimport) int sl_rr_active(void*);
extern "C" __declspec(dllimport) const char* sl_rr_status(void*);
extern "C" __declspec(dllimport) void sl_denoiser(void*,int);
static LRESULT CALLBACK Proc(HWND hwnd,UINT msg,WPARAM w,LPARAM l){if(msg==WM_CLOSE){PostQuitMessage(0);return 0;}return DefWindowProc(hwnd,msg,w,l);}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int){
 int argc;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(argc<6||argc>10)return 2;
 std::wstring packet=argv[1],report=argv[2],shot=argv[3];int width=_wtoi(argv[4]),height=_wtoi(argv[5]);int tier=argc>6?_wtoi(argv[6]):0;bool headless=argc>7&&std::wstring(argv[7])==L"headless";int bounces=argc>8?_wtoi(argv[8]):3;int denoiser=argc>9?_wtoi(argv[9]):1;LocalFree(argv);
 std::ifstream input(packet,std::ios::binary);std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(input)),{});if(bytes.empty())return 3;
 WNDCLASSW wc{};wc.hInstance=instance;wc.lpfnWndProc=Proc;wc.lpszClassName=L"StarlightStandaloneCheck";RegisterClassW(&wc);RECT rect{0,0,width,height};AdjustWindowRect(&rect,WS_OVERLAPPEDWINDOW,FALSE);
 auto hwnd=CreateWindowW(wc.lpszClassName,L"Starlight — matched native test",WS_OVERLAPPEDWINDOW|(headless?0:WS_VISIBLE),50,50,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,instance,nullptr);if(!headless){ShowWindow(hwnd,SW_SHOW);SetForegroundWindow(hwnd);}
 auto fail=[&](int code){std::ofstream output(report);output<<"failure "<<code<<": "<<sl_error();return code;};
 auto context=sl_create(hwnd,width,height);if(!context)return fail(4);if(!sl_scene(context,bytes.data(),(int)bytes.size()))return fail(5);sl_camera(context,0,9.36f,13,0,0,0);sl_rt(context,tier,bounces);sl_freeze(context,1);sl_denoiser(context,denoiser);
 auto start=std::chrono::steady_clock::now(),measure=start;long frames=0;bool measuring=false,captured=false;MSG msg{};std::vector<double> timings;double gpuTotal=0,rayTotal=0,asTotal=0,denoiseTotal=0;long foregroundFrames=0;auto previous=start,focusSince=start;
 while(true){while(PeekMessage(&msg,nullptr,0,0,PM_REMOVE)){if(msg.message==WM_QUIT){sl_destroy(context);DestroyWindow(hwnd);return 6;}TranslateMessage(&msg);DispatchMessage(&msg);}
  auto now=std::chrono::steady_clock::now();double seconds=std::chrono::duration<double>(now-start).count();if(measuring&&std::chrono::duration<double>(now-measure).count()>3)break;if(seconds>40)return fail(9);
  if(!headless&&GetForegroundWindow()!=hwnd)focusSince=now;
  if(std::chrono::duration<double>(now-focusSince).count()>1&&!captured){sl_shot(context,shot.c_str());captured=true;}
  if(std::chrono::duration<double>(now-focusSince).count()>2&&!measuring){measure=now;measuring=true;frames=0;}
  if(!sl_frame(context,0,13))return fail(7);auto end=std::chrono::steady_clock::now();if(measuring){frames++;if(GetForegroundWindow()==hwnd)foregroundFrames++;timings.push_back(std::chrono::duration<double,std::milli>(end-previous).count());gpuTotal+=sl_gpu_ms(context);rayTotal+=sl_ray_ms(context);denoiseTotal+=sl_denoise_ms(context);asTotal+=sl_as_ms(context);}previous=end;
 }
 double duration=std::chrono::duration<double>(std::chrono::steady_clock::now()-measure).count();std::sort(timings.begin(),timings.end());auto percentile=[&](double p){return timings.empty()?0:timings[std::min(timings.size()-1,(size_t)(p*timings.size()))];};std::ofstream output(report);output<<"{\"success\":true,\"fps\":"<<frames/duration<<",\"foregroundFrames\":"<<foregroundFrames<<",\"frames\":"<<frames<<",\"width\":"<<width<<",\"height\":"<<height<<",\"renderWidth\":"<<sl_render_width(context)<<",\"gpuMs\":"<<gpuTotal/std::max(1L,frames)<<",\"p95Ms\":"<<percentile(.95)<<",\"p99Ms\":"<<percentile(.99)<<",\"upscaling\":false,\"frameGeneration\":false,\"foreground\":"<<(GetForegroundWindow()==hwnd?"true":"false")<<"}";output.close();
 // Append telemetry before the final brace, preserving the existing report fields.
 std::ifstream previousReport(report);std::string json((std::istreambuf_iterator<char>(previousReport)),{});previousReport.close();if(!json.empty())json.pop_back();std::ofstream extended(report);extended<<json<<",\"requestedTier\":"<<tier<<",\"activeTier\":"<<sl_rt_active(context)<<",\"rtSupported\":"<<(sl_rt_supported(context)?"true":"false")<<",\"rayDispatchMs\":"<<rayTotal/std::max(1L,frames)<<",\"tlasMs\":"<<asTotal/std::max(1L,frames)<<",\"denoiseMs\":"<<denoiseTotal/std::max(1L,frames)<<",\"blasBuilds\":"<<sl_blas_builds(context)<<",\"tlasRefits\":"<<sl_tlas_refits(context)<<",\"samples\":"<<sl_rt_samples(context)<<",\"bounces\":"<<bounces<<",\"headless\":"<<(headless?"true":"false")<<",\"rrActive\":"<<(sl_rr_active(context)?"true":"false")<<",\"rrStatus\":\""<<sl_rr_status(context)<<"\",\"denoiserRequested\":"<<denoiser<<",\"frozenSnapshot\":true}";extended.close();sl_destroy(context);DestroyWindow(hwnd);return 0;
}
