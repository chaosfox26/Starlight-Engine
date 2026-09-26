#pragma once
#include "platform.hpp"
#include "adventure.hpp"
namespace starlight::editor {
class CombatHud {
    HWND window_{};
    AdventurePlayer::CombatDisplay value_;
    int scale_=100;
    int px(int n)const{return MulDiv(n,scale_,100);}
    static LRESULT CALLBACK proc(HWND h,UINT m,WPARAM w,LPARAM l){
        auto p=reinterpret_cast<CombatHud*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){p=static_cast<CombatHud*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);p->window_=h;SetWindowLongPtrW(h,GWLP_USERDATA,(LONG_PTR)p);}
        if(p && m==WM_PAINT){p->paint();return 0;}
        if(m==WM_NCHITTEST)return HTTRANSPARENT;
        if(m==WM_ERASEBKGND)return 1;
        return DefWindowProcW(h,m,w,l);
    }
    void paint(){
        PAINTSTRUCT ps{};auto dc=BeginPaint(window_,&ps);RECT r{};GetClientRect(window_,&r);
        auto bg=CreateSolidBrush(RGB(20,25,42));FillRect(dc,&r,bg);DeleteObject(bg);
        auto font=CreateFontW(-px(14),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,DEFAULT_QUALITY,DEFAULT_PITCH,L"Segoe UI");
        auto old=SelectObject(dc,font);SetBkMode(dc,TRANSPARENT);SetTextColor(dc,RGB(240,243,250));
        auto label=[&](int y,const std::wstring& text){TextOutW(dc,px(10),px(y),text.c_str(),int(text.size()));};
        auto bar=[&](int y,double fraction,COLORREF color){
            RECT b{px(10),px(y),px(310),px(y+7)};auto brush=CreateSolidBrush(RGB(55,62,78));FillRect(dc,&b,brush);DeleteObject(brush);
            b.right=b.left+int(px(300)*std::clamp(fraction,0.,1.));brush=CreateSolidBrush(color);FillRect(dc,&b,brush);DeleteObject(brush);
        };
        label(5,L"Health  "+std::to_wstring(int(std::ceil(value_.health)))+L" / 100");bar(24,value_.health/100,RGB(75,210,155));
        static const wchar_t* names[]={L"Stunned",L"Knockback",L"Pulled",L"Launched",L"Knocked down",L"Rooted",L"Slowed",L"Silenced",L"Feared",L"Taunted",L"Sleeping",L"Blinded",L"Disoriented",L"Tethered"};
        if(value_.effect)label(36,std::wstring(value_.waking?L"Waking up":names[int(*value_.effect)])+L"  "+std::to_wstring(int(std::ceil(value_.effectSeconds)))+L"s");
        else if(value_.immunitySeconds>0)label(36,L"CC immune  "+std::to_wstring(int(std::ceil(value_.immunitySeconds)))+L"s");
        else if(value_.health<=0)label(36,L"Defeated");
        if(value_.hasTarget){
            label(57,std::wstring(value_.boss?L"Boss":L"Target")+L"  "+std::to_wstring(int(std::ceil(value_.targetHealth)))+L" / "+std::to_wstring(int(std::ceil(value_.targetMaximum))));
            bar(76,value_.targetMaximum>0?value_.targetHealth/value_.targetMaximum:0,RGB(230,95,85));
            if(value_.boss){
                auto phase=value_.phase;
                label(89,phase==BossBreakBar::Phase::Ready?L"Break strength":(phase==BossBreakBar::Phase::Staggered?L"Staggered: +25% damage  ":L"Recovering  ")+std::to_wstring(int(std::ceil(value_.phaseSeconds)))+L"s");
                bar(110,value_.breakStrength/value_.breakMaximum,phase==BossBreakBar::Phase::Recovering?RGB(130,135,150):RGB(95,180,255));
            }
        }
        SelectObject(dc,old);DeleteObject(font);EndPaint(window_,&ps);
    }
public:
    void create(HWND parent){
        WNDCLASSW wc{};wc.lpfnWndProc=proc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"StarlightCombatHud";RegisterClassW(&wc);
        window_=CreateWindowExW(WS_EX_LAYERED|WS_EX_TRANSPARENT|WS_EX_NOACTIVATE,wc.lpszClassName,L"Combat status",WS_CHILD,0,0,320,125,parent,nullptr,wc.hInstance,this);
        SetLayeredWindowAttributes(window_,0,235,LWA_ALPHA);
    }
    void update(const AdventurePlayer* player,HWND viewport,int scale=100){
        if(!window_)return;if(!player){ShowWindow(window_,SW_HIDE);return;}
        value_=player->combat_display();scale_=std::clamp(scale,25,200);
        RECT r{};GetWindowRect(viewport,&r);POINT origin{r.left,r.top};ScreenToClient(GetParent(window_),&origin);
        int height=value_.boss?125:value_.hasTarget?89:57;
        SetWindowPos(window_,HWND_TOP,origin.x+std::max(0,int(r.right-r.left)/2-px(160)),origin.y+px(100),px(320),px(height),SWP_NOACTIVATE|SWP_SHOWWINDOW);
        InvalidateRect(window_,nullptr,FALSE);
    }
};
}
