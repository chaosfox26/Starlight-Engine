
#include "render_device.h"
#include <shellapi.h>
static RenderDevice& device = *new RenderDevice;
static LRESULT CALLBACK WindowProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(h,m,w,l);
}
static int WINAPI RunProjectBody(HINSTANCE instance, LPWSTR command, int) {
    int argc=0; auto argv=CommandLineToArgvW((L"project "+std::wstring(command)).c_str(),&argc);
    device.benchMs=0; UINT width=0,height=0; std::wstring display;
    for(int i=1;i+1<argc;++i) {
        if(std::wstring(argv[i])==L"--report") { std::wstring p=argv[++i]; device.reportPath=std::string(p.begin(),p.end()); }
        else if(std::wstring(argv[i])==L"--bench") device.benchMs=std::stoull(argv[++i]);
    }
    for(int i=1;i<argc;++i) {
        std::wstring option=argv[i];
        if(option==L"--novsync") device.vsync=false;
        else if(option==L"--fg") device.fgRequest=true;
        else if(i+1<argc) {
            if(option==L"--res") { unsigned x=0,y=0;swscanf_s(argv[++i],L"%ux%u",&x,&y);width=x;height=y; }
            else if(option==L"--display")display=argv[++i];
            else if(option==L"--quality"||option==L"--upscaler") {std::wstring v=argv[++i];std::string value(v.begin(),v.end());if(option==L"--quality")device.modeRequest=value;else device.upsRequest=value;}
            else if(option==L"--nocull")device.cullOn=std::wstring(argv[++i])==L"0";
        }
    }
    LocalFree(argv);
    if(!width||!height||display.empty()||!device.benchMs)return 3;
    SetProcessDPIAware();
    WNDCLASSW wc{}; wc.lpfnWndProc=WindowProc;wc.hInstance=instance;wc.lpszClassName=L"StarlightEmptyScene";
    RegisterClassW(&wc);
    DWORD style=display==L"windowed"?WS_OVERLAPPEDWINDOW:WS_POPUP;
    RECT bounds{0,0,(LONG)width,(LONG)height};
    if(display==L"windowed")AdjustWindowRect(&bounds,style,FALSE);
    HWND h=CreateWindowExW(display==L"fullscreen"?WS_EX_TOPMOST:0,wc.lpszClassName,L"Starlight — empty 3D project",style,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
    if(!h)return 2;

    Mesh mesh;BuildBox(mesh);
    try {
        device.init(h,width,height,mesh);RenderDevice::activateProjectWindow(h);
        MSG msg{};
        while(msg.message!=WM_QUIT) {
            if(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            else device.renderEmpty(h);
        }
        device.teardown();DestroyWindow(h);return 0;
    }catch(const std::exception&){device.teardown();DestroyWindow(h);return 2;}
}

extern "C" __declspec(dllexport) int WINAPI StarlightRunProject(HINSTANCE i, LPWSTR c, int s) { int result=RunProjectBody(i,c,s); delete &device; return result; }
