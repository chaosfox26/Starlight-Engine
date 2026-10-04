#pragma once
#include <windows.h>
#include <gdiplus.h>
#include <memory>

class PackageArtwork {
    IStream* stream=nullptr;
    std::unique_ptr<Gdiplus::Bitmap> bitmap;
public:
    PackageArtwork()=default;
    PackageArtwork(const PackageArtwork&)=delete;
    PackageArtwork& operator=(const PackageArtwork&)=delete;
    ~PackageArtwork(){reset();}
    void reset(){bitmap.reset();if(stream){stream->Release();stream=nullptr;}}
    bool load(HMODULE module,int id){
        reset();auto resource=FindResourceW(module,MAKEINTRESOURCEW(id),RT_RCDATA);
        if(!resource)return false;
        auto size=SizeofResource(module,resource);auto source=LockResource(LoadResource(module,resource));
        if(!size || !source)return false;
        auto memory=GlobalAlloc(GMEM_MOVEABLE,size);if(!memory)return false;
        auto target=GlobalLock(memory);if(!target){GlobalFree(memory);return false;}
        memcpy(target,source,size);GlobalUnlock(memory);
        if(FAILED(CreateStreamOnHGlobal(memory,TRUE,&stream))){GlobalFree(memory);return false;}
        bitmap.reset(Gdiplus::Bitmap::FromStream(stream));
        if(!bitmap || bitmap->GetLastStatus()!=Gdiplus::Ok){reset();return false;}
        return true;
    }
    explicit operator bool() const{return bitmap!=nullptr;}
    void draw(Gdiplus::Graphics& g,const Gdiplus::Rect& target,const Gdiplus::Rect& source){
        if(!bitmap)return;
        g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        g.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHalf);
        g.DrawImage(bitmap.get(),target,source.X,source.Y,source.Width,source.Height,Gdiplus::UnitPixel);
    }
};

class PackageButtonSurface {
    HDC destination=nullptr,buffer=nullptr;
    HBITMAP bitmap=nullptr;
    HGDIOBJ previous=nullptr;
    RECT bounds{};
public:
    explicit PackageButtonSurface(const DRAWITEMSTRUCT& item):destination(item.hDC),bounds(item.rcItem){
        int width=bounds.right-bounds.left,height=bounds.bottom-bounds.top;
        if(width<=0 || height<=0)return;
        buffer=CreateCompatibleDC(destination);
        if(buffer)bitmap=CreateCompatibleBitmap(destination,width,height);
        if(bitmap)previous=SelectObject(buffer,bitmap);
    }
    PackageButtonSurface(const PackageButtonSurface&)=delete;
    PackageButtonSurface& operator=(const PackageButtonSurface&)=delete;
    explicit operator bool() const{return bitmap!=nullptr;}
    HDC dc() const{return buffer;}
    void present(){if(bitmap)BitBlt(destination,bounds.left,bounds.top,bounds.right-bounds.left,bounds.bottom-bounds.top,buffer,0,0,SRCCOPY);}
    ~PackageButtonSurface(){if(previous)SelectObject(buffer,previous);if(bitmap)DeleteObject(bitmap);if(buffer)DeleteDC(buffer);}
};
