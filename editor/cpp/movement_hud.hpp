#pragma once
#include "platform.hpp"
#include "movement_resources.hpp"
namespace starlight::editor {
class MovementHud {
    HWND window_{};
    MovementResources value_;
    int scale_=100;
    int px(int n)const{return MulDiv(n,scale_,100);}
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto p=reinterpret_cast<MovementHud*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){p=static_cast<MovementHud*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);p->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)p);}
        if(p && m==WM_PAINT){p->paint();return 0;}
        if(m==WM_NCHITTEST)return HTTRANSPARENT;
        if(m==WM_ERASEBKGND)return 1;
        return DefWindowProcW(h,m,w,l);
    }
    void paint(){
        PAINTSTRUCT ps{};auto dc=BeginPaint(window_,&ps);RECT r{};GetClientRect(window_,&r);
        auto background=CreateSolidBrush(RGB(1,2,3));FillRect(dc,&r,background);DeleteObject(background);
        auto arc=[&](double fraction,COLORREF color){
            auto pen=CreatePen(PS_SOLID,std::max(1,px(5)),color);auto old=SelectObject(dc,pen);
            POINT points[65]{};int count=std::max(1,int(64*std::clamp(fraction,0.,1.)));
            for(int i=0;i<=count;i++){double a=3.141592653589793*(double(i)/64-.5);points[i]={px(118-int(38*std::cos(a))),px(50+int(38*std::sin(a)))};}
            if(count>1)Polyline(dc,points,count+1);SelectObject(dc,old);DeleteObject(pen);
        };
        arc(1,RGB(45,52,68));arc(value_.stamina/500,value_.exhausted?RGB(235,152,65):RGB(78,219,173));
        auto charge=[&](int y,double fill,COLORREF color){
            RECT bar{px(12),px(y),px(61),px(y+7)};auto brush=CreateSolidBrush(RGB(45,52,68));FillRect(dc,&bar,brush);DeleteObject(brush);
            bar.right=bar.left+int(px(49)*std::clamp(fill,0.,1.));brush=CreateSolidBrush(color);FillRect(dc,&bar,brush);DeleteObject(brush);
        };
        for(int i=0;i<2;i++){
            charge(25+i*12,i<value_.dodgeCharges?1:i==value_.dodgeCharges?value_.dodgeRecharge/5:0,RGB(105,183,255));
            charge(68+i*12,i<value_.grappleCharges?1:i==value_.grappleCharges?value_.grappleRecharge/10:0,RGB(238,197,82));
        }
        SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(235,240,250));auto old=SelectObject(dc,GetStockObject(DEFAULT_GUI_FONT));
        TextOutW(dc,px(12),px(7),L"Dodge",5);TextOutW(dc,px(12),px(50),L"Grapple",7);
        SelectObject(dc,old);EndPaint(window_,&ps);
    }
public:
    void create(HWND parent){
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"StarlightMovementHud";RegisterClassW(&wc);
        window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,wc.lpszClassName,L"Movement resources",WS_CHILD,0,0,130,100,parent,nullptr,wc.hInstance,this);
        SetLayeredWindowAttributes(window_,RGB(1,2,3),255,LWA_COLORKEY);
    }
    void update(const MovementResources* value,HWND viewport,int scale=100){
        if(!window_)return;
        if(!value){ShowWindow(window_,SW_HIDE);return;}
        value_=*value;scale_=std::clamp(scale,25,200);
        RECT r{};GetWindowRect(viewport,&r);POINT origin{r.left,r.top};ScreenToClient(GetParent(window_),&origin);
        int width=px(130),height=px(100);
        SetWindowPos(window_,HWND_TOP,origin.x+std::max(0,int(r.right-r.left)/2-px(200)),origin.y+std::max(0,int(r.bottom-r.top)/2-px(50)),width,height,SWP_NOACTIVATE|SWP_SHOWWINDOW);
        InvalidateRect(window_,nullptr,FALSE);
    }
};
}
