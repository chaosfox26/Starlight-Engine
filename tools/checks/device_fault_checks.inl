// Device-failure checks for the viewport (audits CC-07 and CC-02). The renderer counts down a test hook and reports the n-th checked D3D12 result as failed;
// every such failure must come back as an error (create returns null, a frame returns 0, or destruction records "Viewport cleanup"), never as a crash,
// a silent success or a terminate in a destructor. The module is loaded at run time so the suite does not depend on it at link time.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
static void device_fault_checks(const auto& require){
    // Set while the GPU must stay free (for example during gaming): the device checks are then reported as NOT RUN, which the ledger counts as missing coverage, never as a pass.
    if(std::getenv("STARLIGHT_SKIP_GPU_CHECKS")){std::cout<<"device failure checks: NOT RUN (STARLIGHT_SKIP_GPU_CHECKS is set)"<<std::endl;return;}
    HMODULE module=LoadLibraryW(L"Starlight.Viewport.dll");
    require(module!=nullptr,"CC-07: The viewport module loads for the device-failure checks");
    if(!module)return;
    using Create=void*(*)(HWND,int,int);using Frame=int(*)(void*,double,float);using Destroy=void(*)(void*);using Error=const char*(*)();using Fault=int(*)(int);
    auto create=reinterpret_cast<Create>(GetProcAddress(module,"sl_create"));auto frame=reinterpret_cast<Frame>(GetProcAddress(module,"sl_frame"));
    auto destroy=reinterpret_cast<Destroy>(GetProcAddress(module,"sl_destroy"));auto error=reinterpret_cast<Error>(GetProcAddress(module,"sl_error"));
    auto fault=reinterpret_cast<Fault>(GetProcAddress(module,"sl_debug_fail_hresult"));
    require(create&&frame&&destroy&&error&&fault,"CC-07: The viewport module exports the failure hook and the lifecycle calls");
    if(!(create&&frame&&destroy&&error&&fault))return;
    HWND window=CreateWindowExW(0,L"STATIC",L"Starlight device failure checks",0,0,0,640,360,nullptr,nullptr,nullptr,nullptr);
    require(window!=nullptr,"CC-07: A window exists for the device-failure checks");
    if(!window)return;
    void* clean=create(window,640,360);
    require(clean!=nullptr&&frame(clean,0,13)==1&&frame(clean,0,13)==1,"CC-07: With no injected failure a viewport is created and renders frames");
    if(clean)destroy(clean);
    if(!clean){DestroyWindow(window);return;}
    int fired=0,surfaced=0,cleanup=0;bool exhausted=false;std::string silent;
    for(int n=1;n<=128&&!exhausted;++n){
        fault(n);
        void* ctx=create(window,640,360);
        bool reported=ctx==nullptr&&error()[0]!=0;
        if(ctx){for(int i=0;i<2&&!reported;++i)if(frame(ctx,0,13)!=1)reported=error()[0]!=0;}
        if(ctx){destroy(ctx);}
        const std::string after=error();
        const bool cleanupReported=after.find("Viewport cleanup")!=std::string::npos;
        const bool hit=fault(0)==0;
        if(!hit){exhausted=true;break;}
        ++fired;
        if(reported)++surfaced;else if(cleanupReported)++cleanup;else silent+=" "+std::to_string(n);
    }
    fault(0);
    require(exhausted,"CC-07: Every checked D3D12 result up to the end of the first frames and the destruction was exercised");
    require(fired>=8,"CC-07: A meaningful number of checked results were failed on purpose");
    if(!silent.empty())std::cout<<"device failure checks: injected failure number(s) that were not reported:"<<silent<<'\n';
    require(silent.empty(),"CC-07: Every injected D3D12 failure is reported by create, frame or destruction instead of being ignored");
    require(cleanup>=1,"CC-02: A device failure during destruction is recorded as a cleanup error and does not terminate the process");
    void* again=create(window,640,360);
    require(again!=nullptr&&frame(again,0,13)==1,"CC-07: After every injected failure a fresh viewport still creates and renders");
    if(again)destroy(again);
    DestroyWindow(window);
    std::cout<<"device failure checks: "<<fired<<" failures injected, "<<surfaced<<" reported by create/frame, "<<cleanup<<" by destruction\n";
}
