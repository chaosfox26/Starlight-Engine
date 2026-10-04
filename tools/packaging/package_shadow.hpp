#pragma once
#include <windows.h>
#include <gdiplus.h>

class PackageShadow {
    HWND owner=nullptr,window=nullptr;
    int lastWidth=0,lastHeight=0;
    void render(int width,int height,POINT position){
        HDC screen=GetDC(nullptr),dc=CreateCompatibleDC(screen);
        BITMAPINFO info{};info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);info.bmiHeader.biWidth=width;info.bmiHeader.biHeight=-height;info.bmiHeader.biPlanes=1;info.bmiHeader.biBitCount=32;info.bmiHeader.biCompression=BI_RGB;
        void* pixels=nullptr;HBITMAP bitmap=CreateDIBSection(screen,&info,DIB_RGB_COLORS,&pixels,nullptr,0);
        if(bitmap && dc){
            auto previous=SelectObject(dc,bitmap);
            Gdiplus::Bitmap surface(width,height,width*4,PixelFormat32bppPARGB,static_cast<BYTE*>(pixels));
            {Gdiplus::Graphics g(&surface);g.Clear(Gdiplus::Color(0,0,0,0));g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                for(int spread=24;spread>=1;--spread){float x=float(28-spread),y=float(34-spread),w=float(width-56+2*spread),h=float(height-56+2*spread),d=float(24+spread*2);
                    Gdiplus::GraphicsPath path;path.AddArc(x,y,d,d,180,90);path.AddArc(x+w-d,y,d,d,270,90);path.AddArc(x+w-d,y+h-d,d,d,0,90);path.AddArc(x,y+h-d,d,d,90,90);path.CloseFigure();
                    Gdiplus::SolidBrush ink(Gdiplus::Color(4,0,0,0));g.FillPath(&ink,&path);
                }
                g.SetCompositingMode(Gdiplus::CompositingModeSourceCopy);Gdiplus::SolidBrush clear(Gdiplus::Color(0,0,0,0));g.FillRectangle(&clear,28,28,width-56,height-56);
            }
            SIZE size{width,height};POINT origin{};BLENDFUNCTION blend{AC_SRC_OVER,0,255,AC_SRC_ALPHA};UpdateLayeredWindow(window,screen,&position,&size,dc,&origin,0,&blend,ULW_ALPHA);
            SelectObject(dc,previous);DeleteObject(bitmap);
        }
        if(dc)DeleteDC(dc);if(screen)ReleaseDC(nullptr,screen);
    }
public:
    PackageShadow()=default;
    PackageShadow(const PackageShadow&)=delete;
    ~PackageShadow(){if(window)DestroyWindow(window);}
    void attach(HWND parent){
        owner=parent;WNDCLASSW cls{};cls.lpfnWndProc=DefWindowProcW;cls.hInstance=GetModuleHandleW(nullptr);cls.lpszClassName=L"StarlightSetupShadow";RegisterClassW(&cls);
        window=CreateWindowExW(WS_EX_LAYERED|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE|WS_EX_TRANSPARENT,cls.lpszClassName,L"",WS_POPUP,0,0,0,0,owner,nullptr,cls.hInstance,nullptr);
    }
    void sync(){
        if(!window)return;
        if(!IsWindowVisible(owner) || IsIconic(owner) || IsZoomed(owner)){ShowWindow(window,SW_HIDE);return;}
        RECT r;GetWindowRect(owner,&r);int width=r.right-r.left+56,height=r.bottom-r.top+56;POINT point{r.left-28,r.top-28};
        if(width!=lastWidth || height!=lastHeight){render(width,height,point);lastWidth=width;lastHeight=height;}
        SetWindowPos(window,nullptr,point.x,point.y,width,height,SWP_NOZORDER|SWP_NOACTIVATE|SWP_SHOWWINDOW);
    }
    void close(){if(window){DestroyWindow(window);window=nullptr;}}
};
