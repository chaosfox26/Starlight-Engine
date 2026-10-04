#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <iostream>
#include "../../editor/cpp/boot_splash.hpp"
#pragma comment(lib,"gdiplus.lib")
#pragma comment(lib,"winmm.lib")
#pragma comment(lib,"gdi32.lib")
#pragma comment(lib,"user32.lib")
int wmain(int argc,wchar_t** argv){
 if(argc!=2)return 2;
 auto root=std::filesystem::absolute(argv[1]);
 SetEnvironmentVariableW(L"STARLIGHT_RESOURCE_ROOT",(root/L"out/missing-splash-fixture").c_str());
 {starlight::editor::BootSplash missing; if(missing.active())return 3;}
 SetEnvironmentVariableW(L"STARLIGHT_RESOURCE_ROOT",root.c_str());
 auto began=GetTickCount64();
 {starlight::editor::BootSplash splash;if(!splash.active())return 4;splash.finish();}
 auto elapsed=GetTickCount64()-began;std::cout<<"Splash elapsed milliseconds: "<<elapsed<<"\n";
 return elapsed>=9000 && elapsed<15000?0:5;
}
