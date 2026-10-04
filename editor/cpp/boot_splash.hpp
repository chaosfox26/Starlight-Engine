#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <mmsystem.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <filesystem>
#include <future>
#include <memory>
#include <thread>
#include <vector>

namespace starlight::editor {
class BootSplash {
    std::atomic<HWND> window{nullptr};
    std::atomic<bool> done{true};
    std::atomic<ULONGLONG> dismissedAt{0};
    std::thread ui;
    ULONGLONG started=0;
    static constexpr int width=960,height=540;
    std::unique_ptr<Gdiplus::Bitmap> artwork;
    struct Particle{float x,y,vx,vy,delay,life,size;};
    std::vector<Particle> particles;
    static float smooth(float x){x=std::clamp(x,0.f,1.f);return x*x*(3-2*x);}
    float fadeOutStart() const{
        const ULONGLONG at=dismissedAt.load();
        if(!at)return 8.55f;
        const float seconds=at>started?float(at-started)/1000.f:0.f;
        return std::min(8.55f,std::max(seconds,1.4f));
    }
    static std::filesystem::path resourceRoot(){
        wchar_t buffer[32768];auto n=GetEnvironmentVariableW(L"STARLIGHT_RESOURCE_ROOT",buffer,32768);
        if(n && n<32768 && std::filesystem::path(buffer).is_absolute())return buffer;
        GetModuleFileNameW(nullptr,buffer,32768);auto root=std::filesystem::path(buffer);
        for(int i=0;i<4;++i)root=root.parent_path();return root;
    }
    void makeParticles(){
        unsigned seed=27318;auto random=[&](){seed=seed*1664525u+1013904223u;return float(seed)/4294967296.f;};
        for(int i=0;i<125;++i){float x=0,y=0;Gdiplus::Color color;
            for(int tries=0;tries<10000;++tries){x=52+random()*855;y=105+random()*330;
                artwork->GetPixel(INT((x-37.5f)*artwork->GetWidth()/885.f),INT((y-21)*artwork->GetHeight()/498.f),&color);if(color.GetA()>200)break;}
            float angle=random()*6.283185f,speed=16.5f+random()*67.5f;
            particles.push_back({x,y,std::cos(angle)*speed,std::sin(angle)*speed-6,random()*.32f,.5f+random()*1.3f,.825f+random()*1.725f});
        }
    }
    void paint(HWND h){
        using namespace Gdiplus;
        const float t=float(GetTickCount64()-started)/1000.f;
        const float fade=smooth((t-.15f)/1.25f)*(1-smooth((t-fadeOutStart())/.65f));
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;HDC screen=GetDC(nullptr),memory=CreateCompatibleDC(screen);HBITMAP dib=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(!dib || !memory){if(dib)DeleteObject(dib);if(memory)DeleteDC(memory);ReleaseDC(nullptr,screen);return;}
        auto previous=SelectObject(memory,dib);
        {
            Bitmap frame(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));Graphics g(&frame);
            g.Clear(Color(0,0,0,0));g.SetSmoothingMode(SmoothingModeAntiAlias);g.SetInterpolationMode(InterpolationModeHighQualityBicubic);
            ColorMatrix matrix={1,0,0,0,0, 0,1,0,0,0, 0,0,1,0,0, 0,0,0,fade,0, 0,0,0,0,1};ImageAttributes attributes;attributes.SetColorMatrix(&matrix);
            g.DrawImage(artwork.get(),Rect(38,21,885,498),0,0,artwork->GetWidth(),artwork->GetHeight(),UnitPixel,&attributes);
            g.Flush(FlushIntentionSync);
            // Brighten existing premultiplied pixels only: no rectangular shine background.
            const float sweep=-165+((t-.55f)/5.45f)*(width+330);
            if(t>=.55f && t<=6.f){auto p=static_cast<BYTE*>(pixels);for(int y=0;y<height;++y)for(int x=0;x<width;++x){auto k=(y*width+x)*4;float shine=(std::max)(0.f,1-std::abs(x-sweep)/72.f)*.53f;for(int c=0;c<3;++c)p[k+c]=BYTE(p[k+c]+(p[k+3]-p[k+c])*shine);}}
            for(const auto& p:particles){float age=t-6-p.delay;if(age<0 || age>p.life)continue;float alpha=std::pow(1-age/p.life,1.5f)*smooth(age/.025f)*fade;float x=p.x+p.vx*age,y=p.y+p.vy*age+6*age*age;
                SolidBrush halo(Color(BYTE(alpha*35),150,190,255));g.FillEllipse(&halo,x-p.size*3,y-p.size*3,p.size*6,p.size*6);
                Pen ray(Color(BYTE(alpha*255),235,249,255),.8f);g.DrawLine(&ray,x-p.size*3,y,x+p.size*3,y);g.DrawLine(&ray,x,y-p.size*3,x,y+p.size*3);}
            g.Flush(FlushIntentionSync);
            SIZE size{width,height};POINT source{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};UpdateLayeredWindow(h,screen,nullptr,&size,memory,&source,0,&blend,ULW_ALPHA);
        }
        SelectObject(memory,previous);DeleteObject(dib);DeleteDC(memory);ReleaseDC(nullptr,screen);
    }
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto that=reinterpret_cast<BootSplash*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){that=static_cast<BootSplash*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(that));}
        if(m==WM_TIMER && that){if(GetTickCount64()-that->started>=ULONGLONG(std::lround(that->fadeOutStart()*1000.f))+650){DestroyWindow(h);return 0;}that->paint(h);return 0;}
        if(m==WM_DESTROY){KillTimer(h,1);PostQuitMessage(0);return 0;}
        return DefWindowProcW(h,m,w,l);
    }
public:
    explicit BootSplash(bool enabled=true){
        if(!enabled)return;
        done.store(false);
        std::promise<void> ready;auto signal=ready.get_future();
        ui=std::thread([this,ready=std::move(ready)]()mutable{
            Gdiplus::GdiplusStartupInput input;ULONG_PTR token=0;
            if(Gdiplus::GdiplusStartup(&token,&input,nullptr)!=Gdiplus::Ok){done.store(true);ready.set_value();return;}
            try {
                auto root=resourceRoot();artwork=std::make_unique<Gdiplus::Bitmap>((root/L"design/branding/starlight-alpha-one-cursive-v3.png").c_str());
                if(artwork->GetLastStatus()!=Gdiplus::Ok)throw std::runtime_error("Missing splash artwork");makeParticles();
                WNDCLASSW cls{};cls.lpfnWndProc=proc;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"StarlightBootSplash";RegisterClassW(&cls);
                RECT area{};SystemParametersInfoW(SPI_GETWORKAREA,0,&area,0);
                HWND h=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_TOOLWINDOW|WS_EX_TOPMOST,cls.lpszClassName,L"Starlight Engine",WS_POPUP,(area.left+area.right-width)/2,(area.top+area.bottom-height)/2,width,height,nullptr,nullptr,cls.hInstance,this);
                window.store(h);started=GetTickCount64();
                if(h){paint(h);SetTimer(h,1,33,nullptr);ShowWindow(h,SW_SHOWNOACTIVATE);PlaySoundW((root/L"design/branding/starlight-alpha-one-audio.wav").c_str(),nullptr,SND_FILENAME|SND_ASYNC|SND_NODEFAULT);}
                ready.set_value();
                if(h){MSG msg;while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}}
            }catch(...){try{ready.set_value();}catch(...){} }
            PlaySoundW(nullptr,nullptr,0);window.store(nullptr);artwork.reset();Gdiplus::GdiplusShutdown(token);done.store(true);
        });signal.wait();
    }
    bool active() const{return window.load()!=nullptr;}
    void finish(){if(ui.joinable())ui.join();}
    bool finished() const{return done.load();}
    void dismiss(){ULONGLONG expected=0;dismissedAt.compare_exchange_strong(expected,GetTickCount64());}
    void close(){if(auto h=window.load())PostMessageW(h,WM_CLOSE,0,0);if(ui.joinable())ui.join();}
    ~BootSplash(){close();}
    BootSplash(const BootSplash&)=delete;BootSplash& operator=(const BootSplash&)=delete;
};
}
