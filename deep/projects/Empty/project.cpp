
#include "render_device.h"
#include "host_readiness.h"
#include "host_config.h"
#include "host_notice.h"
#include "keycard_view.h"
#include "keycard_room.h"
#include <shellapi.h>
static RenderDevice& device = *new RenderDevice;
static host_readiness::Readiness ready;
static bool interactive=false;
static host_config::Options hostOptions;
static host_notice::Layer notice;
static std::vector<keycard_room::Part> roomParts;
static std::string Narrow(const std::wstring& text) {
    const int bytes=WideCharToMultiByte(CP_ACP,0,text.c_str(),(int)text.size(),nullptr,0,nullptr,nullptr);
    std::string out((size_t)(bytes>0?bytes:0),'\0');
    if(bytes>0)WideCharToMultiByte(CP_ACP,0,text.c_str(),(int)text.size(),out.data(),bytes,nullptr,nullptr);
    return out;
}
static int WINAPI RunProjectBody(HINSTANCE instance, LPWSTR command, int);
static LRESULT CALLBACK WindowProc(HWND h,UINT m,WPARAM w,LPARAM l) {
    if(m==WM_DESTROY){PostQuitMessage(0);return 0;}
    if(m==WM_KEYDOWN&&w==VK_ESCAPE&&interactive){DestroyWindow(h);return 0;}
    if(m==WM_KEYDOWN&&w==VK_F1&&interactive){notice.toggle();return 0;}
    return DefWindowProcW(h,m,w,l);
}
static int WINAPI RunProjectBody(HINSTANCE instance, LPWSTR command, int) {
    int argc=0; auto argv=CommandLineToArgvW((L"project "+std::wstring(command)).c_str(),&argc);
    device.benchMs=0; UINT width=0,height=0; std::wstring display;
    for(int i=1;i+1<argc;++i) {
        if(std::wstring(argv[i])==L"--report") { std::wstring p=argv[++i]; device.reportPath=utf8FromWide(p); }
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
    {
        std::vector<std::string> tokens;
        for(int i=1;i<argc;++i)tokens.push_back(Narrow(argv[i]));
        std::string optionError;
        if(!host_config::extract(tokens,hostOptions,optionError)){LocalFree(argv);logLine("Empty: "+optionError);return 3;}
    }
    LocalFree(argv);
    logLine(host_config::report(hostOptions));
    interactive=!device.benchMs;
    if(!interactive&&(!width||!height||display.empty()))return 3;
    if(!width||!height){width=1280;height=720;}
    if(display.empty())display=L"windowed";
    SetProcessDPIAware();
    WNDCLASSW wc{}; wc.lpfnWndProc=WindowProc;wc.hInstance=instance;wc.lpszClassName=L"StarlightEmptyScene";
    RegisterClassW(&wc);
    DWORD style=display==L"windowed"?WS_OVERLAPPEDWINDOW:WS_POPUP;
    RECT bounds{0,0,(LONG)width,(LONG)height};
    if(display==L"windowed")AdjustWindowRect(&bounds,style,FALSE);
    HWND h=CreateWindowExW(display==L"fullscreen"?WS_EX_TOPMOST:0,wc.lpszClassName,L"Starlight — empty 3D project",style,0,0,bounds.right-bounds.left,bounds.bottom-bounds.top,nullptr,nullptr,instance,nullptr);
    if(!h)return 2;

    Mesh mesh;BuildBox(mesh);
    ready.begin();
    const size_t boxItem=ready.require("mesh","unit box","built-in");
    if(mesh.verts.empty()||mesh.indices.empty())ready.markFailed(boxItem,"the built-in unit box has no geometry");else ready.markReady(boxItem);
    keycard::KeyCard card;
    std::string cardError;
    const std::string cardFile=keycard::resolveCardPath(hostOptions.cardPath,keycard_room::moduleFolder(reinterpret_cast<const void*>(&RunProjectBody)),"Empty.starscene",cardError);
    const size_t cardItem=ready.require("key card","Empty scene",cardFile.empty()?hostOptions.cardPath:cardFile);
    if(cardError.empty())cardError=keycard::loadKeyCard(cardFile,card);
    if(cardError.empty())ready.markReady(cardItem);else ready.markFailed(cardItem,cardError);
    keycard::reportSkippedContent(card,ready,cardFile.empty()?hostOptions.cardPath:cardFile);
    auto report=[&]{
        const std::string text=ready.diagnostics();
        logLine("Empty cannot continue:\n"+text);
        OutputDebugStringA(("Empty cannot continue:\n"+text).c_str());
        if(interactive){ShowWindow(h,SW_HIDE);MessageBoxA(nullptr,text.c_str(),"Empty could not finish loading",MB_OK|MB_ICONERROR|MB_TOPMOST|MB_SETFOREGROUND);}
    };
    if(ready.hasBlockingItems()){report();DestroyWindow(h);return 2;}
    try {
        device.init(h,width,height,mesh);
        {
            device.alloc->Reset();device.cmd->Reset(device.alloc.Get(),nullptr);
            roomParts=keycard_room::build(device,card);
            device.cmd->Close();ID3D12CommandList* lists[]={device.cmd.Get()};device.queue->ExecuteCommandLists(1,lists);
            keycard_room::applyCamera(device,card);
            if(card.view.present){hostOptions.derive("camera (field of view, planes"+std::string(card.view.follow?", eye and target":"")+")");logLine(host_config::report(hostOptions));}
            const keycard::Lighting lighting=card.lighting;
            device.editorDraw=[lighting]{
                FrameCB fc{};
                fc.lightDir=XMFLOAT4(lighting.keyDirection[0],lighting.keyDirection[1],lighting.keyDirection[2],lighting.keyIntensity);
                fc.ambient=XMFLOAT4(lighting.ambient.r,lighting.ambient.g,lighting.ambient.b,lighting.ambientGround);
                fc.camPos=XMFLOAT4(device.hostEye.x,device.hostEye.y,device.hostEye.z,0.0f);
                fc.fog=XMFLOAT4(lighting.clear.r,lighting.clear.g,lighting.clear.b,lighting.fogMix);
                device.writeFrameCB(fc);
                keycard_room::draw(device,roomParts,0.0f);
            };
        }
        if(device.msaa>1){
            ComPtr<ID3D12GraphicsCommandList1> resolveList;
            if(FAILED(device.cmd.As(&resolveList)))ready.fail("MSAA "+std::to_string(device.msaa)+"x was selected but the graphics driver does not provide the command list interface needed to resolve it");
        }
        ready.markUploadSubmitted();
        ready.settle(device,60000.0,[]{
            MSG pending{};
            while(PeekMessageW(&pending,nullptr,0,0,PM_REMOVE)){
                if(pending.message==WM_QUIT)return false;
                TranslateMessage(&pending);DispatchMessageW(&pending);
            }
            return true;
        });
        logLine(ready.summary());
        if(!ready.ready()){
            const bool cancelled=ready.state()==host_readiness::State::Cancelled;
            if(!cancelled)report();
            device.teardown();DestroyWindow(h);
            return cancelled?0:2;
        }
        device.uploadKeepAlive.clear();
        if(ready.warningCount()){
            logLine("content warnings:\n"+ready.diagnostics());
            SetWindowTextW(h,(std::wstring(L"Starlight - empty 3D project  |  ")+std::to_wstring(ready.warningCount())+L" content warnings (see the log)").c_str());
        }
        if(interactive){
            std::vector<std::string> info;
            if(!hostOptions.given.empty()){std::string line="Flags from the maintained host configuration:";for(const std::string& flag:hostOptions.given)line+=" "+flag;info.push_back(line);}
            notice.attach(h,instance);notice.update(ready.warnings(),info);
        }
        RenderDevice::activateProjectWindow(h);
        MSG msg{};
        while(msg.message!=WM_QUIT) {
            if(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)){TranslateMessage(&msg);DispatchMessageW(&msg);}
            else{device.renderEmpty(h);notice.follow();}
        }
        notice.close();device.teardown();DestroyWindow(h);return 0;
    }catch(const std::exception& error){ready.fail(std::string("graphics initialisation failed: ")+error.what());report();device.teardown();DestroyWindow(h);return 2;}
}

extern "C" __declspec(dllexport) int WINAPI StarlightRunProject(HINSTANCE i, LPWSTR c, int s) { int result=RunProjectBody(i,c,s); delete &device; return result; }
