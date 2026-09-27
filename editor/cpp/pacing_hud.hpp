#pragma once
#include "native_session.hpp"
#include <sstream>
#include <iomanip>
namespace starlight::editor {
class PacingHud {
    HWND window_{};PacingSnapshot snapshot_;CameraState camera_;int scale_=100;
    ULONGLONG refreshed_=0;
    int px(int n)const{return MulDiv(n,scale_,100);}
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto p=reinterpret_cast<PacingHud*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){p=static_cast<PacingHud*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);p->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)p);}
        if(p && m==WM_PAINT){p->paint();return 0;}
        if(m==WM_NCHITTEST)return HTTRANSPARENT;
        if(m==WM_ERASEBKGND)return 1;
        return DefWindowProcW(h,m,w,l);
    }
    void paint(){
        PAINTSTRUCT ps{};auto target=BeginPaint(window_,&ps);RECT r{};GetClientRect(window_,&r);
        auto dc=CreateCompatibleDC(target);auto bitmap=CreateCompatibleBitmap(target,std::max(1L,r.right),std::max(1L,r.bottom));auto oldBitmap=SelectObject(dc,bitmap);
        auto fill=[&](RECT box,COLORREF color){auto b=CreateSolidBrush(color);FillRect(dc,&box,b);DeleteObject(b);};
        fill(r,RGB(1,2,3));fill({px(14),px(14),px(700),px(395)},RGB(18,25,38));
        SetBkMode(dc,TRANSPARENT);auto font=CreateFontW(-px(15),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");auto oldFont=SelectObject(dc,font);
        auto text=[&](int x,int y,const std::wstring& s,COLORREF color=RGB(236,243,251)){SetTextColor(dc,color);TextOutW(dc,x,y,s.c_str(),int(s.size()));};
        auto number=[](double n){std::wostringstream s;s<<std::fixed<<std::setprecision(1)<<n;return s.str();};
        text(px(26),px(23),L"Motion & frame timing    F8 to hide");
        text(px(26),px(47),L"Render: "+number(snapshot_.fps)+L" FPS   Frame time: "+number(snapshot_.p50)+L" ms median");
        text(px(26),px(69),L"95% "+number(snapshot_.p95)+L" ms   99% "+number(snapshot_.p99)+L" ms   Worst "+number(snapshot_.worst)+L" ms");
        double ceiling=std::max(20.,snapshot_.worst);
        int left=px(28),top=px(98),width=px(505),height=px(76);
        auto grid=CreatePen(PS_SOLID,1,RGB(75,85,102));auto oldPen=SelectObject(dc,grid);
        for(double ms:{5.,10.,16.667}){int y=top+height-int(height*ms/ceiling);MoveToEx(dc,left,y,nullptr);LineTo(dc,left+width,y);}
        SelectObject(dc,oldPen);DeleteObject(grid);
        auto pen=CreatePen(PS_SOLID,std::max(1,px(2)),RGB(93,220,188));oldPen=SelectObject(dc,pen);
        std::vector<POINT> points;for(size_t i=0;i<snapshot_.frameMs.size();++i)points.push_back({left+int(i*width/std::max(size_t(1),snapshot_.frameMs.size()-1)),top+height-int(height*snapshot_.frameMs[i]/ceiling)});
        if(points.size()>1)Polyline(dc,points.data(),int(points.size()));SelectObject(dc,oldPen);DeleteObject(pen);
        text(px(26),px(180),L"Last 2 seconds | 0 - "+number(ceiling)+L" ms | CPU frame intervals");
        text(px(26),px(201),L"1% low: "+number(snapshot_.low1)+L" FPS   0.1% low: "+number(snapshot_.low01)+L" FPS");
        text(px(26),px(222),L"Object labels: pose updates/s. Still objects show STATIC.");
        if(snapshot_.logFailed || snapshot_.droppedLogRecords)text(px(26),px(244),L"Timing log incomplete - check disk / dropped samples",RGB(255,120,100));
        const auto& work=snapshot_.multicore;
        text(px(26),px(266),L"Renderer pool (last frame): "+std::to_wstring(work.parallelBatches)+L" parallel / "+std::to_wstring(work.serialBatches)+L" serial batches");
        text(px(26),px(287),L"Participants incl. caller: "+std::to_wstring(work.participants)+L"   Work: "+number(work.wallMs)+L" ms   Join: "+number(work.callerWaitMs)+L" ms");
        text(px(26),px(308),work.parallelBatches+work.serialBatches==0?L"No renderer pool work this frame (not whole-CPU utilization)":L"Per-worker elapsed times are saved in the timing log.");
        const auto& battle=snapshot_.battleWorkers;
        text(px(26),px(334),L"Battle visuals: "+std::to_wstring(battle.participants)+L" participants   "+number(battle.wallMs)+L" ms   Join: "+number(battle.callerWaitMs)+L" ms");
        text(px(26),px(355),L"Player/boss effects: "+std::to_wstring(battle.parallelBatches)+L" parallel / "+std::to_wstring(battle.serialBatches)+L" serial batches");
        std::wstring chunks=L"Worker slots (caller first): ";for(double ms:battle.chunkMs)chunks+=number(ms)+L" ms  ";text(px(26),px(376),chunks);
        const auto forward=(camera_.target-camera_.eye).unit();const auto right=forward.cross({0,1,0}).unit();const auto up=right.cross(forward).unit();
        const double focal=r.right/(2*std::tan(48.*3.141592653589793/360));
        int labelCount=0;
        for(const auto& o:snapshot_.objects){
            if(o.id=="camera") {text(px(585),px(23),L"Camera: "+(o.moving?number(o.updatesPerSecond)+L" updates/s":L"STATIC"));continue;}
            auto delta=o.position-camera_.eye;double depth=delta.dot(forward);if(depth<.1)continue;
            int x=int(r.right*.5+delta.dot(right)*focal/depth),y=int(r.bottom*.5-delta.dot(up)*focal/depth);
            if(x<0||x>=r.right||y<0||y>=r.bottom)continue;
            std::wstring label=wide(o.name)+L": "+(o.moving?number(o.updatesPerSecond)+L" updates/s":L"STATIC");
            SIZE extent{};GetTextExtentPoint32W(dc,label.c_str(),int(label.size()),&extent);
            x=std::clamp(x-int(extent.cx)/2,0,std::max(0,int(r.right-extent.cx-px(12))));y=std::clamp(y-px(28),0,std::max(0,int(r.bottom-px(27))));
            fill({x,y,x+extent.cx+px(12),y+px(25)},RGB(18,25,38));text(x+px(6),y+px(3),label,o.moving?RGB(255,211,107):RGB(189,203,220));++labelCount;
        }
        BitBlt(target,0,0,r.right,r.bottom,dc,0,0,SRCCOPY);SelectObject(dc,oldFont);DeleteObject(font);SelectObject(dc,oldBitmap);DeleteObject(bitmap);DeleteDC(dc);EndPaint(window_,&ps);
    }
public:
    void create(HWND parent){WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"StarlightPacingHud";RegisterClassW(&wc);
        window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,wc.lpszClassName,L"Motion and frame timing",WS_CHILD,0,0,1,1,parent,nullptr,wc.hInstance,this);
        SetLayeredWindowAttributes(window_,RGB(1,2,3),215,LWA_COLORKEY|LWA_ALPHA);
    }
    void update(bool enabled,NativeSession& renderer,HWND view,CameraState camera,int scale){
        if(!window_)return;if(!enabled){ShowWindow(window_,SW_HIDE);return;}
        auto now=GetTickCount64();if(now-refreshed_<100)return;refreshed_=now;
        snapshot_=renderer.pacing_snapshot();camera_=camera;scale_=scale;
        RECT r{};GetWindowRect(view,&r);POINT origin{r.left,r.top};ScreenToClient(GetParent(window_),&origin);
        SetWindowPos(window_,HWND_TOP,origin.x,origin.y,r.right-r.left,r.bottom-r.top,SWP_NOACTIVATE|SWP_SHOWWINDOW);InvalidateRect(window_,nullptr,FALSE);
    }
};
}
