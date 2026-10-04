#pragma once
#include <windows.h>
#include <commctrl.h>
#include <cwchar>

// Shared presentation only: no effect on renderer settings or test verdicts.
namespace audit_theme {
inline constexpr COLORREF background = RGB(29,33,47);
inline constexpr COLORREF surface = RGB(43,48,68);
inline constexpr COLORREF foreground = RGB(226,232,247);
inline HBRUSH brush() { static HBRUSH value=CreateSolidBrush(background); return value; }
inline void apply(HWND window) {
    using Theme=HRESULT(WINAPI*)(HWND,LPCWSTR,LPCWSTR);
    static auto theme=reinterpret_cast<Theme>(GetProcAddress(LoadLibraryW(L"uxtheme.dll"),"SetWindowTheme"));
    wchar_t name[64]{}; GetClassNameW(window,name,64);
    auto buttonType=GetWindowLongPtrW(window,GWL_STYLE)&BS_TYPEMASK;
    if(std::wcscmp(name,L"Button")==0 && (buttonType==BS_PUSHBUTTON || buttonType==BS_DEFPUSHBUTTON))
        SetWindowLongPtrW(window,GWL_STYLE,(GetWindowLongPtrW(window,GWL_STYLE)&~BS_TYPEMASK)|BS_OWNERDRAW);
    if(std::wcscmp(name,PROGRESS_CLASSW)==0) {
        if(theme) theme(window,L"",L"");
        SendMessageW(window,PBM_SETBKCOLOR,0,surface);
        SendMessageW(window,PBM_SETBARCOLOR,0,RGB(177,105,222));
    } else if(theme) theme(window,L"DarkMode_Explorer",nullptr);
    using Attribute=HRESULT(WINAPI*)(HWND,DWORD,LPCVOID,DWORD);
    static auto attribute=reinterpret_cast<Attribute>(GetProcAddress(LoadLibraryW(L"dwmapi.dll"),"DwmSetWindowAttribute"));
    if(attribute) { BOOL dark=TRUE; attribute(window,20,&dark,sizeof(dark)); attribute(window,35,&background,sizeof(background)); }
}
inline BOOL CALLBACK child(HWND window,LPARAM) { apply(window); return TRUE; }
inline void apply_tree(HWND window) { apply(window); EnumChildWindows(window,child,0); }
inline bool message(UINT message,WPARAM w,LPARAM l,LRESULT& result) {
    if(message==WM_CTLCOLORSTATIC || message==WM_CTLCOLOREDIT || message==WM_CTLCOLORBTN) {
        auto dc=reinterpret_cast<HDC>(w); SetTextColor(dc,foreground); SetBkColor(dc,background);
        result=reinterpret_cast<LRESULT>(brush()); return true;
    }
    if(message==WM_DRAWITEM) {
        auto item=reinterpret_cast<DRAWITEMSTRUCT*>(l);
        if(item->CtlType!=ODT_BUTTON) return false;
        FillRect(item->hDC,&item->rcItem,brush());
        auto fill=CreateSolidBrush(item->itemState&ODS_SELECTED ? RGB(67,59,94):surface);
        auto pen=CreatePen(PS_SOLID,1,RGB(83,91,120));
        auto oldBrush=SelectObject(item->hDC,fill); auto oldPen=SelectObject(item->hDC,pen);
        RoundRect(item->hDC,item->rcItem.left,item->rcItem.top,item->rcItem.right,item->rcItem.bottom,10,10);
        SelectObject(item->hDC,oldPen); SelectObject(item->hDC,oldBrush); DeleteObject(pen); DeleteObject(fill);
        wchar_t text[256]{}; GetWindowTextW(item->hwndItem,text,256);
        SetBkMode(item->hDC,TRANSPARENT); SetTextColor(item->hDC,item->itemState&ODS_DISABLED ? RGB(132,139,158):foreground);
        auto font=reinterpret_cast<HFONT>(SendMessageW(item->hwndItem,WM_GETFONT,0,0));
        auto oldFont=font?SelectObject(item->hDC,font):nullptr;
        DrawTextW(item->hDC,text,-1,&item->rcItem,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        if(oldFont) SelectObject(item->hDC,oldFont);
        if(item->itemState&ODS_FOCUS) { auto focus=item->rcItem; InflateRect(&focus,-4,-4); DrawFocusRect(item->hDC,&focus); }
        result=TRUE; return true;
    }
    return false;
}
}
