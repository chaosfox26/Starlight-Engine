#pragma once
#include <dwmapi.h>
#include <gdiplus.h>
#include <memory>
#include "package_surface.hpp"
#include "package_theme.hpp"
#include "package_shadow.hpp"

class PackageWindow {
    ULONG_PTR graphicsToken=0;
    PackageShadow shadow;
    bool activeWindow=true;
    int baseWidth=760,baseHeight=490;
    IStream* logoStream=nullptr;
    IStream* backdropStream=nullptr;
    std::unique_ptr<Gdiplus::Bitmap> backdrop;
    std::unique_ptr<Gdiplus::Bitmap> logo;
    HFONT font=nullptr;
    HBRUSH editBrush=CreateSolidBrush(RGB(18,23,40));
    std::wstring heading,description;
    bool progressView=false;
    int fraction=0;
    struct ControlPlacement { HWND handle;int id,x,y,width,height; };
    std::vector<ControlPlacement> placements;
    float contentScale(const RECT& area) const {return std::max(1.f,std::min(float(area.right)/760.f,float(area.bottom-40)/450.f));}
    void layoutControls(HWND h) {
        RECT area;GetClientRect(h,&area);float scale=contentScale(area);
        float left=(area.right-760*scale)/2,top=40+(area.bottom-40-450*scale)/2;
        for(auto& c:placements){
            int x=int(left+c.x*scale),y=int(top+(c.y-40)*scale),width=int(c.width*scale),height=int(c.height*scale);
            if(c.id>=6000){x=area.right-(c.id==6001?142:c.id==6003?96:50);y=2;width=c.width;height=c.height;}
            SetWindowPos(c.handle,nullptr,x,y,width,height,SWP_NOZORDER|SWP_NOACTIVATE);
        }
    }
    void titleSurface(Gdiplus::Graphics& g,int width) {
        Gdiplus::LinearGradientBrush shine(Gdiplus::Point(0,0),Gdiplus::Point(width,40),Gdiplus::Color(255,26,34,51),Gdiplus::Color(255,35,29,57));
        Gdiplus::Color stops[]={Gdiplus::Color(255,28,34,48),Gdiplus::Color(255,14,23,39),Gdiplus::Color(255,35,29,57)};Gdiplus::REAL positions[]={0,.5f,1};shine.SetInterpolationColors(stops,positions,3);g.FillRectangle(&shine,0,0,width,40);
        Gdiplus::LinearGradientBrush gloss(Gdiplus::Point(0,0),Gdiplus::Point(0,40),Gdiplus::Color(22,208,216,255),Gdiplus::Color(0,208,216,255));g.FillRectangle(&gloss,0,0,width,40);
    }
    void backgroundSurface(Gdiplus::Graphics& g,int width,int height) {
        Gdiplus::SolidBrush base(Gdiplus::Color(255,4,9,21));g.FillRectangle(&base,0,0,width,height);
        if(backdrop){float scale=std::max(float(width)/backdrop->GetWidth(),float(height-40)/backdrop->GetHeight());float w=backdrop->GetWidth()*scale,h=backdrop->GetHeight()*scale;
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);g.DrawImage(backdrop.get(),(width-w)/2,40+(height-40-h)/2,w,h);}
    }
    static LRESULT CALLBACK buttonProcedure(HWND h,UINT m,WPARAM w,LPARAM l,UINT_PTR,DWORD_PTR) {
        if(m==WM_ERASEBKGND)return 1;
        if(m==WM_MOUSEMOVE && !GetPropW(h,L"StarlightHover")){SetPropW(h,L"StarlightHover",reinterpret_cast<HANDLE>(1));TRACKMOUSEEVENT track{sizeof(track),TME_LEAVE,h,0};TrackMouseEvent(&track);InvalidateRect(h,nullptr,FALSE);}
        if(m==WM_MOUSELEAVE){RemovePropW(h,L"StarlightHover");InvalidateRect(h,nullptr,FALSE);}
        if(m==WM_NCDESTROY){RemovePropW(h,L"StarlightHover");RemoveWindowSubclass(h,buttonProcedure,1);}
        return DefSubclassProc(h,m,w,l);
    }
    void spacedText(Gdiplus::Graphics& g,const std::wstring& value,float size,float spacing,Gdiplus::RectF box,Gdiplus::Color color,bool bold=false) {
        Gdiplus::Font f(bold?L"Segoe UI Semibold":L"Segoe UI",size,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);Gdiplus::StringFormat format(Gdiplus::StringFormat::GenericTypographic());format.SetFormatFlags(format.GetFormatFlags()|Gdiplus::StringFormatFlagsMeasureTrailingSpaces);
        float width=0;std::vector<float> advances;
        for(wchar_t c:value){Gdiplus::RectF measured;g.MeasureString(&c,1,&f,Gdiplus::PointF(0,0),&format,&measured);advances.push_back(measured.Width+spacing);width+=measured.Width+spacing;}
        Gdiplus::SolidBrush ink(color);Gdiplus::LinearGradientBrush blue(Gdiplus::PointF(box.X,box.Y),Gdiplus::PointF(box.GetRight(),box.Y),Gdiplus::Color(255,61,102,178),Gdiplus::Color(255,130,194,245));
        Gdiplus::Brush* lettering=value==L"POWERING BRIGHTER WORLDS"?static_cast<Gdiplus::Brush*>(&blue):static_cast<Gdiplus::Brush*>(&ink);float x=box.X+(box.Width-width+spacing)/2;
        for(size_t i=0;i<value.size();++i){g.DrawString(&value[i],1,&f,Gdiplus::PointF(x,box.Y+(box.Height-size)/2),&format,lettering);x+=advances[i];}
    }
    static LRESULT CALLBACK procedure(HWND h,UINT m,WPARAM w,LPARAM l) {
        auto p=reinterpret_cast<PackageWindow*>(GetWindowLongPtrW(h,GWLP_USERDATA));
        if(m==WM_NCCREATE){p=static_cast<PackageWindow*>(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams);SetWindowLongPtrW(h,GWLP_USERDATA,reinterpret_cast<LONG_PTR>(p));}
        if(!p)return DefWindowProcW(h,m,w,l);
        if(m==WM_NCCALCSIZE && w)return 0;
        if(m==WM_NCHITTEST){POINT point{static_cast<short>(LOWORD(l)),static_cast<short>(HIWORD(l))};ScreenToClient(h,&point);RECT r;GetClientRect(h,&r);
            if(!IsZoomed(h)){bool left=point.x<6,right=point.x>=r.right-6,top=point.y<6,bottom=point.y>=r.bottom-6;if(top)return left?HTTOPLEFT:right?HTTOPRIGHT:HTTOP;if(bottom)return left?HTBOTTOMLEFT:right?HTBOTTOMRIGHT:HTBOTTOM;if(left)return HTLEFT;if(right)return HTRIGHT;}
            if(point.y<40)return HTCAPTION;return HTCLIENT;}
        if(m==WM_ACTIVATE){p->activeWindow=LOWORD(w)!=WA_INACTIVE;InvalidateRect(h,nullptr,FALSE);for(auto& c:p->placements)InvalidateRect(c.handle,nullptr,FALSE);return 0;}
        if(m==WM_CLOSE){p->choice=IDCANCEL;return 0;}
        if(m==WM_COMMAND && HIWORD(w)==BN_CLICKED){if(LOWORD(w)==6001){ShowWindow(h,SW_MINIMIZE);return 0;}if(LOWORD(w)==6003){ShowWindow(h,IsZoomed(h)?SW_RESTORE:SW_MAXIMIZE);return 0;}p->choice=LOWORD(w)==6002?IDCANCEL:LOWORD(w);return 0;}
        if(m==WM_GETMINMAXINFO){auto info=reinterpret_cast<MINMAXINFO*>(l);info->ptMinTrackSize={p->baseWidth,p->baseHeight};MONITORINFO monitor{sizeof(monitor)};GetMonitorInfoW(MonitorFromWindow(h,MONITOR_DEFAULTTONEAREST),&monitor);info->ptMaxPosition={monitor.rcWork.left-monitor.rcMonitor.left,monitor.rcWork.top-monitor.rcMonitor.top};info->ptMaxSize={monitor.rcWork.right-monitor.rcWork.left,monitor.rcWork.bottom-monitor.rcWork.top};return 0;}
        if(m==WM_SIZE && w!=SIZE_MINIMIZED){int width=LOWORD(l),height=HIWORD(l);SetWindowRgn(h,w==SIZE_MAXIMIZED?nullptr:CreateRoundRectRgn(0,0,width,height,24,24),TRUE);p->layoutControls(h);InvalidateRect(h,nullptr,FALSE);}
        if(m==WM_WINDOWPOSCHANGED){p->shadow.sync();}
        if(m==WM_PRINTCLIENT){p->paint(reinterpret_cast<HDC>(w));return 0;}
        if(m==WM_ERASEBKGND)return 1;
        if(m==WM_CTLCOLOREDIT){SetTextColor(reinterpret_cast<HDC>(w),RGB(245,247,255));SetBkColor(reinterpret_cast<HDC>(w),RGB(18,23,40));return reinterpret_cast<LRESULT>(p->editBrush);}
        if(m==WM_DRAWITEM){p->button(*reinterpret_cast<DRAWITEMSTRUCT*>(l));return TRUE;}
        if(m==WM_PAINT){PAINTSTRUCT ps;auto dc=BeginPaint(h,&ps);p->paint(dc);EndPaint(h,&ps);return 0;}
        return DefWindowProcW(h,m,w,l);
    }
    void text(Gdiplus::Graphics& g,const std::wstring& value,float size,Gdiplus::RectF rect,Gdiplus::Color color,bool center=true,bool bold=false) {
        Gdiplus::Font f(bold?L"Segoe UI Semibold":L"Segoe UI",size,Gdiplus::FontStyleRegular,Gdiplus::UnitPixel);
        Gdiplus::SolidBrush brush(color);Gdiplus::StringFormat format;
        format.SetAlignment(center?Gdiplus::StringAlignmentCenter:Gdiplus::StringAlignmentNear);
        format.SetLineAlignment(Gdiplus::StringAlignmentCenter);
        g.DrawString(value.c_str(),-1,&f,rect,&format,&brush);
    }
    void rounded(Gdiplus::GraphicsPath& path,float x,float y,float width,float height,float radius) {
        float d=radius*2;path.AddArc(x,y,d,d,180,90);path.AddArc(x+width-d,y,d,d,270,90);path.AddArc(x+width-d,y+height-d,d,d,0,90);path.AddArc(x,y+height-d,d,d,90,90);path.CloseFigure();
    }
    void paint(HDC dc) {
        RECT bounds;GetClientRect(window,&bounds);
        float width=float(bounds.right),height=float(bounds.bottom);
        Gdiplus::Bitmap buffer(bounds.right*2,bounds.bottom*2);Gdiplus::Graphics g(&buffer);g.ScaleTransform(2,2);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);
        backgroundSurface(g,bounds.right,bounds.bottom);titleSurface(g,bounds.right);
        Gdiplus::GraphicsPath frame;rounded(frame,1.5f,1.5f,width-4,height-4,12);
        for(int spread=6;spread>=2;spread-=2){Gdiplus::Pen glow(Gdiplus::Color(4,60,149,255),float(spread));g.DrawPath(&glow,&frame);}
        Gdiplus::LinearGradientBrush rim(Gdiplus::Point(0,0),Gdiplus::Point(bounds.right,bounds.bottom),Gdiplus::Color(90,105,162,225),Gdiplus::Color(80,152,113,202));Gdiplus::Pen border(&rim,.8f);g.DrawPath(&border,&frame);
        if(logo)g.DrawImage(logo.get(),16,10,20,20);
        text(g,L"Starlight Engine Setup",14,{44,9,350,22},activeWindow?Gdiplus::Color(255,222,230,242):Gdiplus::Color(255,132,138,151),false);
        auto bodyState=g.Save();float scale=contentScale(bounds);g.TranslateTransform((width-760*scale)/2,40+(height-40-450*scale)/2-40*scale);g.ScaleTransform(scale,scale);width=760;
        float size=progressView?116.f:208.f;
        if(logo)g.DrawImage(logo.get(),(width-size)/2,progressView?56.f:54.f,size,size);
        if(!progressView && heading==L"Starlight Engine")spacedText(g,heading,40,1.4f,{28,267,width-56,48},Gdiplus::Color(255,228,237,255),true);
        else text(g,heading,progressView?32.f:40.f,{28,progressView?184.f:267.f,width-56,48},Gdiplus::Color(255,235,240,255),true,true);
        if(description==L"CREATE  ·  ANALYZE  ·  EXPLORE  ·  BRIGHTER")spacedText(g,L"CREATE · ANALYZE · EXPLORE · BRIGHTER",12,3.f,{34,316,width-68,32},Gdiplus::Color(255,155,201,255));
        else text(g,description,17,{34,progressView?232.f:312.f,width-68,42},Gdiplus::Color(255,191,206,230));
        if(progressView){
            Gdiplus::SolidBrush track(Gdiplus::Color(255,29,34,53));g.FillRectangle(&track,48,296,664,20);
            Gdiplus::LinearGradientBrush fill(Gdiplus::Point(40,0),Gdiplus::Point(bounds.right-40,0),Gdiplus::Color(255,210,58,210),Gdiplus::Color(255,80,183,255));
            g.FillRectangle(&fill,48,296,664*fraction/1000,20);
        }
        g.Restore(bodyState);width=float(bounds.right);
        text(g,L"ChaosFox",12,{30,height-44,160,22},Gdiplus::Color(255,150,166,192),false);
        spacedText(g,L"POWERING BRIGHTER WORLDS",10,1.9f,{width-294,height-38,264,20},Gdiplus::Color(255,102,180,241));
        Gdiplus::Graphics screen(dc);screen.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);screen.DrawImage(&buffer,0,0,int(bounds.right),int(bounds.bottom));
    }
    void button(const DRAWITEMSTRUCT& item) {
        PackageButtonSurface surface(item);
        if(!surface){paintButton(item);return;}
        DRAWITEMSTRUCT buffered=item;buffered.hDC=surface.dc();buffered.rcItem={0,0,item.rcItem.right-item.rcItem.left,item.rcItem.bottom-item.rcItem.top};
        paintButton(buffered);surface.present();
    }

    void paintButton(const DRAWITEMSTRUCT& item) {
        Gdiplus::Graphics g(item.hDC);auto r=item.rcItem;bool down=(item.itemState&ODS_SELECTED)!=0;
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);g.SetTextRenderingHint(Gdiplus::TextRenderingHintAntiAlias);g.Clear(Gdiplus::Color(255,9,12,26));
        RECT position,area;GetWindowRect(item.hwndItem,&position);MapWindowPoints(nullptr,window,reinterpret_cast<POINT*>(&position),2);GetClientRect(window,&area);
        auto backgroundState=g.Save();g.TranslateTransform(float(-position.left),float(-position.top));
        if(item.CtlID>=6000)titleSurface(g,area.right);else backgroundSurface(g,area.right,area.bottom);
        g.Restore(backgroundState);
        bool primary=item.CtlID==102,neutral=item.CtlID==IDCANCEL || item.CtlID==IDOK || item.CtlID>=6000;
        bool hover=GetPropW(item.hwndItem,L"StarlightHover")!=nullptr;
        if(item.CtlID>=6000){
            if(hover || down){Gdiplus::GraphicsPath highlight;rounded(highlight,3,3,float(r.right-6),float(r.bottom-6),6);Gdiplus::SolidBrush tint(item.CtlID==6002?Gdiplus::Color(down?230:190,185,30,50):Gdiplus::Color(down?60:32,210,220,240));g.FillPath(&tint,&highlight);}
            Gdiplus::Pen pen(activeWindow?Gdiplus::Color(255,230,237,250):Gdiplus::Color(255,130,138,155),1.2f);float cx=r.right*.5f,cy=r.bottom*.5f;
            if(item.CtlID==6001)g.DrawLine(&pen,cx-5,cy,cx+5,cy);
            else if(item.CtlID==6003){g.DrawRectangle(&pen,cx-4,cy-4,8.f,8.f);if(IsZoomed(window)){g.DrawLine(&pen,cx-1,cy-7,cx+7,cy-7);g.DrawLine(&pen,cx+7,cy-7,cx+7,cy+1);}}
            else {g.DrawLine(&pen,cx-4,cy-4,cx+4,cy+4);g.DrawLine(&pen,cx+4,cy-4,cx-4,cy+4);}
            if((item.itemState&ODS_FOCUS) && !(item.itemState&ODS_NOFOCUSRECT)){Gdiplus::Pen focus(Gdiplus::Color(160,135,192,255),1.4f);g.DrawRectangle(&focus,5.f,5.f,float(r.right-10),float(r.bottom-10));}return;
        }
        float scale=contentScale(area);g.ScaleTransform(scale,scale);r.right=LONG(r.right/scale);r.bottom=LONG(r.bottom/scale);
        Gdiplus::Color a=neutral?Gdiplus::Color(255,47,60,86):primary?Gdiplus::Color(255,230,0,194):Gdiplus::Color(255,0,165,248);
        Gdiplus::Color b=neutral?Gdiplus::Color(255,19,26,44):primary?Gdiplus::Color(255,102,0,139):Gdiplus::Color(255,0,36,182);
        if(down){a=Gdiplus::Color(255,25,25,55);b=Gdiplus::Color(255,10,14,35);}
        float press=down?2.f:0.f;
        g.TranslateTransform(0,press);
        Gdiplus::GraphicsPath shape;rounded(shape,3+(down?1.f:0.f),3,float(r.right-7-(down?2:0)),float(r.bottom-7),10);
        Gdiplus::LinearGradientBrush fill(Gdiplus::Point(0,0),Gdiplus::Point(0,r.bottom),a,b);g.FillPath(&fill,&shape);
        if(!neutral && !down){
            Gdiplus::Bitmap surface(r.right,r.bottom,PixelFormat32bppARGB);Gdiplus::BitmapData data{};Gdiplus::Rect region(0,0,r.right,r.bottom);
            if(surface.LockBits(&region,Gdiplus::ImageLockModeWrite,PixelFormat32bppARGB,&data)==Gdiplus::Ok){
                int corners[2][4][3]={{{246,5,198},{170,1,184},{153,0,135},{100,0,114}},{{0,141,255},{0,91,240},{0,45,172},{0,40,158}}};auto& c=corners[primary?0:1];
                for(int y=0;y<r.bottom;++y){double v=std::min(1.,double(y)/std::max<LONG>(1,r.bottom-1)/.85);auto row=reinterpret_cast<unsigned char*>(data.Scan0)+y*data.Stride;
                    for(int x=0;x<r.right;++x){double u=double(x)/std::max<LONG>(1,r.right-1);for(int channel=0;channel<3;++channel){double top=c[0][channel]*(1-u)+c[1][channel]*u,bottom=c[2][channel]*(1-u)+c[3][channel]*u;row[x*4+2-channel]=BYTE(top*(1-v)+bottom*v);}row[x*4+3]=255;}}
                surface.UnlockBits(&data);auto saved=g.Save();g.SetClip(&shape);g.DrawImage(&surface,0,0);g.Restore(saved);
            }
        }
        Gdiplus::Pen glow(Gdiplus::Color(down?5:20,primary?255:64,primary?46:190,255),down?1.f:3.f);g.DrawPath(&glow,&shape);
        Gdiplus::Pen edge(Gdiplus::Color(180,primary?255:70,primary?80:185,255),.75f);g.DrawPath(&edge,&shape);
        wchar_t title[128];GetWindowTextW(item.hwndItem,title,128);
        if(!down){auto saved=g.Save();g.SetClip(&shape);Gdiplus::LinearGradientBrush glass(Gdiplus::Point(0,3),Gdiplus::Point(0,r.bottom/2),Gdiplus::Color(28,255,255,255),Gdiplus::Color(0,255,255,255));g.FillRectangle(&glass,3,3,int(r.right-6),int(r.bottom/2-3));g.Restore(saved);}
        if(down){Gdiplus::Pen inset(Gdiplus::Color(160,0,0,12),3.f);auto saved=g.Save();g.SetClip(&shape);g.DrawLine(&inset,5.f,5.f,float(r.right-6),5.f);g.Restore(saved);}
        bool option=item.CtlID==101 || item.CtlID==102;
        if(option){Gdiplus::Pen mark(Gdiplus::Color(255,255,255,255),2.5f);float cx=34,cy=r.bottom*.5f;
            if(item.CtlID==102){g.DrawLine(&mark,cx,cy-10,cx,cy+4);g.DrawLine(&mark,cx-5,cy-1,cx,cy+4);g.DrawLine(&mark,cx,cy+4,cx+5,cy-1);g.DrawLine(&mark,cx-9,cy+7,cx-9,cy+11);g.DrawLine(&mark,cx-9,cy+11,cx+9,cy+11);g.DrawLine(&mark,cx+9,cy+11,cx+9,cy+7);}
            else {Gdiplus::PointF points[]={{cx-7,cy-11},{cx+10,cy},{cx-7,cy+11}};Gdiplus::SolidBrush white(Gdiplus::Color(255,255,255,255));g.FillPolygon(&white,points,3);}}
        text(g,title,19,{option?58.f:6.f,0,float(r.right-(option?70:12)),float(r.bottom)},Gdiplus::Color(255,250,250,255),true,true);
        if((item.itemState&ODS_FOCUS) && !(item.itemState&ODS_NOFOCUSRECT)){Gdiplus::Pen focus(Gdiplus::Color(120,172,212,255),.8f);g.DrawPath(&focus,&shape);}
    }
public:
    HWND window=nullptr,entry=nullptr;
    int choice=0;
    PackageWindow(std::wstring title,std::wstring detail,bool progress=false):heading(std::move(title)),description(std::move(detail)),progressView(progress) {
        Gdiplus::GdiplusStartupInput startup;Gdiplus::GdiplusStartup(&graphicsToken,&startup,nullptr);
        auto module=GetModuleHandleW(nullptr);auto resource=FindResourceW(module,MAKEINTRESOURCEW(102),RT_RCDATA);
        if(resource){auto size=SizeofResource(module,resource);auto source=LockResource(LoadResource(module,resource));auto memory=GlobalAlloc(GMEM_MOVEABLE,size);if(memory){auto target=GlobalLock(memory);memcpy(target,source,size);GlobalUnlock(memory);if(SUCCEEDED(CreateStreamOnHGlobal(memory,TRUE,&logoStream)))logo.reset(Gdiplus::Bitmap::FromStream(logoStream));else GlobalFree(memory);}}
        resource=FindResourceW(module,MAKEINTRESOURCEW(103),RT_RCDATA);
        if(resource){auto size=SizeofResource(module,resource);auto source=LockResource(LoadResource(module,resource));auto memory=GlobalAlloc(GMEM_MOVEABLE,size);if(memory){auto target=GlobalLock(memory);memcpy(target,source,size);GlobalUnlock(memory);if(SUCCEEDED(CreateStreamOnHGlobal(memory,TRUE,&backdropStream)))backdrop.reset(Gdiplus::Bitmap::FromStream(backdropStream));else GlobalFree(memory);}}
        WNDCLASSW cls{};cls.lpfnWndProc=procedure;cls.hInstance=module;cls.lpszClassName=L"StarlightGlassSetup";cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);cls.hIcon=LoadIconW(module,MAKEINTRESOURCEW(101));RegisterClassW(&cls);
        int width=baseWidth,height=baseHeight;
        window=CreateWindowExW(WS_EX_CONTROLPARENT,cls.lpszClassName,L"Starlight Engine Setup",WS_POPUP|WS_THICKFRAME|WS_SYSMENU|WS_MINIMIZEBOX|WS_MAXIMIZEBOX|WS_CLIPCHILDREN,(GetSystemMetrics(SM_CXSCREEN)-width)/2,(GetSystemMetrics(SM_CYSCREEN)-height)/2,width,height,nullptr,nullptr,module,this);
        if(!window)throw std::runtime_error("Cannot open Starlight setup window");
        SetWindowRgn(window,CreateRoundRectRgn(0,0,width,height,24,24),TRUE);
        COLORREF noBorder=0xFFFFFFFE;DwmSetWindowAttribute(window,34,&noBorder,sizeof(noBorder));
        BOOL dark=TRUE;DwmSetWindowAttribute(window,20,&dark,sizeof(dark));int corners=2,backdrop=3;DwmSetWindowAttribute(window,33,&corners,sizeof(corners));DwmSetWindowAttribute(window,38,&backdrop,sizeof(backdrop));
        shadow.attach(window);
        font=CreateFontW(-17,0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,0,0,CLEARTYPE_QUALITY,0,L"Segoe UI");
        {addButton(6001,L"Minimize",width-142,2,46,36);addButton(6003,L"Maximize or restore",width-96,2,46,36);addButton(6002,L"Close",width-50,2,46,36);}
    }
    void addButton(int id,const wchar_t* label,int x,int y,int width=310,int height=60){auto b=CreateWindowW(L"BUTTON",label,WS_CHILD|WS_VISIBLE|WS_TABSTOP|BS_OWNERDRAW,x,y,width,height,window,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(b,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);placements.push_back({b,id,x,y,width,height});SetWindowSubclass(b,buttonProcedure,1,0);layoutControls(window);}
    void addEntry(const std::wstring& value){entry=CreateWindowExW(WS_EX_CLIENTEDGE,L"EDIT",value.c_str(),WS_CHILD|WS_VISIBLE|WS_TABSTOP|ES_AUTOHSCROLL,48,352,664,30,window,nullptr,GetModuleHandleW(nullptr),nullptr);SendMessageW(entry,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);placements.push_back({entry,0,48,352,664,30});}
    std::wstring value(){int n=GetWindowTextLengthW(entry);std::wstring s(n+1,L'\0');GetWindowTextW(entry,s.data(),n+1);s.resize(n);return s;}
    void show(){ShowWindow(window,SW_SHOW);UpdateWindow(window);shadow.sync();}
    void pump(){MSG m;while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){if(m.message==WM_KEYDOWN && m.wParam==VK_ESCAPE && (m.hwnd==window || IsChild(window,m.hwnd))){choice=IDCANCEL;continue;}if(!IsDialogMessageW(window,&m)){TranslateMessage(&m);DispatchMessageW(&m);}}}
    int run(){show();while(!choice){pump();if(!choice)MsgWaitForMultipleObjects(0,nullptr,FALSE,50,QS_ALLINPUT);}return choice;}
    void update(const std::wstring& detail,int amount){description=detail;fraction=std::clamp(amount,0,1000);InvalidateRect(window,nullptr,FALSE);UpdateWindow(window);pump();}
    ~PackageWindow(){shadow.close();if(window)DestroyWindow(window);if(font)DeleteObject(font);DeleteObject(editBrush);logo.reset();if(logoStream)logoStream->Release();backdrop.reset();if(backdropStream)backdropStream->Release();if(graphicsToken)Gdiplus::GdiplusShutdown(graphicsToken);}
};

int packageChoice(){PackageWindow view(L"Starlight Engine",L"CREATE  ·  ANALYZE  ·  EXPLORE  ·  BRIGHTER");view.addButton(102,L"Install on this PC",52,365,316);view.addButton(101,L"Run portable",392,365,316);return view.run();}
int packageMessage(const std::wstring& title,const std::wstring& message,bool confirmation=false){PackageWindow view(title,message);view.addButton(IDOK,confirmation?L"Uninstall":L"OK",confirmation?48:225,365);if(confirmation)view.addButton(IDCANCEL,L"Cancel",402,365);return view.run();}
