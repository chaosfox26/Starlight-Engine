// Device-failure checks for the viewport (audits CC-07 and CC-02). The renderer counts down a test hook and reports the n-th checked D3D12 result as failed;
// every such failure must come back as an error (create returns null, a frame returns 0, or destruction records "Viewport cleanup"), never as a crash,
// a silent success or a terminate in a destructor. The module is loaded at run time so the suite does not depend on it at link time.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fstream>
#include <iterator>
static void device_fault_checks(const auto& require){
    // Set while the GPU must stay free (for example during gaming): the device checks are then reported as NOT RUN, which the ledger counts as missing coverage, never as a pass.
    if(std::getenv("STARLIGHT_SKIP_GPU_CHECKS")){std::cout<<"device failure checks: NOT RUN (STARLIGHT_SKIP_GPU_CHECKS is set)"<<std::endl;return;}
    // The renderer opens its log on the first viewport of the process; RND-8 reads it to see that a lost device is reported.
    std::wstring lossLog;{wchar_t base[MAX_PATH]{};GetTempPathW(MAX_PATH,base);lossLog=std::wstring(base)+L"starlight-device-loss-"+std::to_wstring(GetCurrentProcessId())+L".log";}
    DeleteFileW(lossLog.c_str());_wputenv_s(L"STARLIGHT_NATIVE_LOG",lossLog.c_str());
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
    // RND-2: a resize that fails part-way must leave the device in its previous state, so the next frame renders and a retry of the same resize can succeed.
    using Resize=int(*)(void*,int,int);auto resize=reinterpret_cast<Resize>(GetProcAddress(module,"sl_resize"));
    require(resize!=nullptr,"RND-2: The viewport module exports sl_resize");
    if(resize){
        void* ctx=create(window,640,360);
        require(ctx!=nullptr&&frame(ctx,0,13)==1,"RND-2: A viewport exists for the resize failure sweep");
        int resizeFired=0;std::string broken;
        for(int n=1;ctx&&n<=40;++n){
            fault(n);
            const int result=resize(ctx,800,450);
            const bool hit=fault(0)==0;fault(0);
            if(hit){
                ++resizeFired;
                if(result!=0){broken+=" "+std::to_string(n)+"(failure not reported)";continue;}
                if(frame(ctx,0,13)!=1)broken+=" "+std::to_string(n)+"(frame after failed resize)";
                if(resize(ctx,800,450)!=1||frame(ctx,0,13)!=1)broken+=" "+std::to_string(n)+"(retry)";
                if(resize(ctx,640,360)!=1||frame(ctx,0,13)!=1)broken+=" "+std::to_string(n)+"(size back)";
            }else{
                require(result==1&&frame(ctx,0,13)==1,"RND-2: A resize that no injected failure reaches succeeds and renders");
                if(resize(ctx,640,360)!=1)broken+=" "+std::to_string(n)+"(size back after success)";
                break;
            }
        }
        if(!broken.empty())std::cout<<"resize failure sweep: broken after injected failure number(s):"<<broken<<'\n';
        require(resizeFired>=3,"RND-2: Several checked results inside a resize were failed on purpose");
        require(broken.empty(),"RND-2: After every injected failure inside a resize the viewport keeps rendering at its previous size and the resize can be retried");
        if(ctx)destroy(ctx);
    }
    // RND-3: a frame that throws while its command list is recording must not wedge every later frame.
    {
        void* ctx=create(window,640,360);
        require(ctx!=nullptr&&frame(ctx,0,13)==1,"RND-3: A viewport exists for the frame failure sweep");
        int frameFired=0;std::string wedged;
        for(int n=1;ctx&&n<=80;++n){
            fault(n);
            const int result=frame(ctx,0,13);
            const bool hit=fault(0)==0;fault(0);
            if(!hit)break;
            ++frameFired;
            if(result==1)continue;
            if(frame(ctx,0,13)!=1&&frame(ctx,0,13)!=1)wedged+=" "+std::to_string(n);
        }
        if(!wedged.empty())std::cout<<"frame failure sweep: frames kept failing after injected failure number(s):"<<wedged<<'\n';
        require(frameFired>=8,"RND-3: Several checked results inside a frame were failed on purpose");
        require(wedged.empty(),"RND-3: After a failure inside a frame the next frames render again");
        if(ctx)destroy(ctx);
    }
    // RND-8: a removed device is reported (with the DRED breadcrumbs) by the first failing call, and the viewport can still be destroyed and replaced.
    using Remove=int(*)(void*);auto remove=reinterpret_cast<Remove>(GetProcAddress(module,"sl_debug_remove_device"));
    require(remove!=nullptr,"RND-8: The viewport module exports the device removal hook");
    if(remove){
        void* ctx=create(window,640,360);
        require(ctx!=nullptr&&frame(ctx,0,13)==1&&remove(ctx)==1,"RND-8: A viewport exists and its device could be removed on purpose");
        if(ctx){
            require(frame(ctx,0,13)==0&&error()[0]!=0,"RND-8: A frame on a removed device fails with an error");
            destroy(ctx);
        }
        void* replacement=create(window,640,360);
        require(replacement!=nullptr&&frame(replacement,0,13)==1,"RND-8: A new viewport renders after another one lost its device");
        if(replacement)destroy(replacement);
        std::ifstream log(lossLog);std::string text((std::istreambuf_iterator<char>(log)),std::istreambuf_iterator<char>());
        if(text.empty())std::cout<<"device removal report: NOT RUN (the renderer log was opened earlier in this process)"<<std::endl;
        else require(text.find("device removed HRESULT")!=std::string::npos,"RND-8: The first failing call on a removed device writes the device removal report to the log");
    }
    DestroyWindow(window);
    std::cout<<"device failure checks: "<<fired<<" failures injected, "<<surfaced<<" reported by create/frame, "<<cleanup<<" by destruction\n";
}
