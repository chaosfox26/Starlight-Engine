#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <stdexcept>
#include <vector>
#include "audit_theme.hpp"

namespace fs = std::filesystem;
static HWND statusLabel, progressBar, openReport, openFolder, startButton, errorButton, bothButton, outputLabel, openComparison;
static std::vector<std::wstring> launchArgs;
static void startAnalysis(HWND hwnd, const wchar_t* mode=L"features");
static HANDLE child = nullptr;
static fs::path output;
static bool finished = true;

static std::wstring wide(const std::string& text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), nullptr, 0);
    std::wstring value(n, 0);
    if(n) MultiByteToWideChar(CP_UTF8, 0, text.data(), (int)text.size(), value.data(), n);
    return value;
}
static std::wstring quote(const std::wstring& arg) {
    std::wstring out=L"\""; size_t slashes=0;
    for(auto c:arg) {
        if(c==L'\\') { ++slashes; continue; }
        out.append(c==L'"'?slashes*2+1:slashes,L'\\'); slashes=0; out+=c;
    }
    out.append(slashes*2,L'\\'); return out+L'"';
}
static HWND control(HWND parent,const wchar_t* cls,const wchar_t* text,DWORD style,int x,int y,int w,int h,int id=0) {
    auto handle=CreateWindowW(cls,text,WS_CHILD|WS_VISIBLE|style,x,y,w,h,parent,(HMENU)(INT_PTR)id,GetModuleHandleW(nullptr),nullptr);
    SendMessageW(handle,WM_SETFONT,(WPARAM)GetStockObject(DEFAULT_GUI_FONT),TRUE); return handle;
}
static LRESULT CALLBACK procedure(HWND hwnd,UINT message,WPARAM w,LPARAM l) {
    LRESULT themed{}; if(audit_theme::message(message,w,l,themed)) return themed;
    switch(message) {
    case WM_CREATE:
        control(hwnd,L"STATIC",L"Starlight Engine — Audit and Reporting",0,24,20,700,28);
        control(hwnd,L"STATIC",L"Feature report inventories code and saved evidence. Error checks build and test.\nRun both combines them into one shareable report; visible tests need foreground focus.",0,24,56,700,44);
        progressBar=control(hwnd,PROGRESS_CLASSW,L"",PBS_SMOOTH,24,117,700,24);
        SendMessageW(progressBar,PBM_SETRANGE32,0,100);
        statusLabel=control(hwnd,L"STATIC",L"Ready. Choose Feature report, Error checks, or Run both.",0,24,156,700,64);
        outputLabel=control(hwnd,L"EDIT",output.c_str(),ES_READONLY|ES_AUTOHSCROLL|WS_BORDER,24,231,700,28);
        openReport=control(hwnd,L"BUTTON",L"Open shareable report",BS_PUSHBUTTON,24,282,158,32,101);
        openFolder=control(hwnd,L"BUTTON",L"Open evidence folder",BS_PUSHBUTTON,188,282,170,32,102);
        openComparison=control(hwnd,L"BUTTON",L"Open engine comparison",BS_PUSHBUTTON,370,282,190,32,106);
        EnableWindow(openComparison,FALSE);
        EnableWindow(openReport,FALSE);
        startButton=control(hwnd,L"BUTTON",L"Feature report",BS_PUSHBUTTON,24,326,180,32,103);
        errorButton=control(hwnd,L"BUTTON",L"Error checks",BS_PUSHBUTTON,216,326,180,32,104);
        bothButton=control(hwnd,L"BUTTON",L"Run both",BS_PUSHBUTTON,408,326,180,32,105);
        control(hwnd,L"STATIC",L"Each run preserves its own evidence. Builds can take several minutes.\nCompletion means reports were saved, not that every feature passed.",0,24,374,700,42);
        audit_theme::apply_tree(hwnd);
        return 0;
    case WM_TIMER: {
        // The worker atomically replaces this file. Permit replacement while
        // reading and close before updating controls to avoid a sharing race.
        std::string snapshot;
        HANDLE progressFile = CreateFileW((output/L"progress.txt").c_str(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (progressFile != INVALID_HANDLE_VALUE) {
            char buffer[8192]; DWORD read = 0;
            if (ReadFile(progressFile, buffer, sizeof(buffer), &read, nullptr))
                snapshot.assign(buffer, read);
            CloseHandle(progressFile);
        }
        std::istringstream f(snapshot);
        std::string first, rest;
        if(std::getline(f,first)) {
            rest.assign(std::istreambuf_iterator<char>(f),{});
            try { SendMessageW(progressBar,PBM_SETPOS,(WPARAM)std::max(0,std::stoi(first)),0); } catch(...) {}
            SetWindowTextW(statusLabel,wide(rest).c_str());
        }
        DWORD code=STILL_ACTIVE;
        if(child && GetExitCodeProcess(child,&code) && code!=STILL_ACTIVE) {
            EnableWindow(startButton,TRUE); EnableWindow(errorButton,TRUE); EnableWindow(bothButton,TRUE);
            finished=true; KillTimer(hwnd,1); CloseHandle(child); child=nullptr;
            bool exists=fs::is_regular_file(output/L"AUDIT-SHAREABLE-REPORT.md");
            EnableWindow(openReport,exists);
            EnableWindow(openComparison,fs::is_regular_file(output/L"ENGINE-COMPARISON.md"));
            if(code==4 && exists) SetWindowTextW(statusLabel,L"Audit completed with failed, unavailable or untested stages. Open the shareable report for findings.");
            else if(code!=0 || !exists) SetWindowTextW(statusLabel,L"Analysis failed or stopped before producing a summary. See worker.log and failure.txt.");
            SetWindowTextW(hwnd,code==0&&exists?L"Starlight analysis — Completed":L"Starlight analysis — Needs attention");
        }
        return 0;
    }
    case WM_COMMAND:
        if(LOWORD(w)==103 && !child) startAnalysis(hwnd,L"features");
        if(LOWORD(w)==104 && !child) startAnalysis(hwnd,L"errors");
        if(LOWORD(w)==105 && !child) startAnalysis(hwnd,L"both");
        if(LOWORD(w)==101) ShellExecuteW(hwnd,L"open",(output/L"AUDIT-SHAREABLE-REPORT.md").c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        if(LOWORD(w)==106 && fs::is_regular_file(output/L"ENGINE-COMPARISON.md")) ShellExecuteW(hwnd,L"open",(output/L"ENGINE-COMPARISON.md").c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        if(LOWORD(w)==102) ShellExecuteW(hwnd,L"open",output.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
        return 0;
    case WM_CLOSE:
        if(!finished) { ShowWindow(hwnd,SW_MINIMIZE); return 0; }
        DestroyWindow(hwnd); return 0;
    case WM_DESTROY: PostQuitMessage(0); return 0;
    }
    return DefWindowProcW(hwnd,message,w,l);
}
static void startAnalysis(HWND hwnd, const wchar_t* mode) {
    if(child) return;
    EnableWindow(openComparison,FALSE);
    try {
        fs::path base=fs::path(launchArgs[3])/L"analysis";
        SYSTEMTIME t{}; GetLocalTime(&t); wchar_t name[128]{};
        swprintf_s(name,L"run-%04u%02u%02u-%02u%02u%02u-%lu-%llu",t.wYear,t.wMonth,t.wDay,t.wHour,t.wMinute,t.wSecond,GetCurrentProcessId(),GetTickCount64());
        output=launchArgs.size()==5 ? fs::path(launchArgs[4]) : base/name;
        if(launchArgs.size()==5) launchArgs.resize(4);
        if(!fs::create_directories(output)) throw std::runtime_error("Output exists");
        auto scripts=fs::path(launchArgs[2]).parent_path();
        fs::copy_file(scripts/L"audit_engine.py",output/L"analyzer-used.py");
        fs::copy_file(scripts/L"deep_analysis.py",output/L"deep_analysis.py");
        fs::copy_file(scripts/L"knowledge_report.py",output/L"knowledge_report.py");
        fs::copy_file(scripts/L"feature_evidence.py",output/L"feature_evidence.py");
        fs::copy_file(scripts/L"engine_comparison.py",output/L"engine_comparison.py");
    } catch(...) { SetWindowTextW(statusLabel,L"Could not prepare fresh evidence folder or snapshot the reporting scripts."); return; }
    SetWindowTextW(outputLabel,output.c_str());
    SetWindowTextW(statusLabel,L"Starting analysis...");
    SetWindowTextW(hwnd,L"Starlight analysis — Running");
    SendMessageW(progressBar,PBM_SETPOS,0,0); EnableWindow(openReport,FALSE);
    std::wstring command=quote(launchArgs[1])+L" "+quote((output/L"analyzer-used.py").wstring())+L" --root "+quote(launchArgs[3])+L" --output "+quote(output.wstring())+L" --audit-mode "+quote(mode);
    SECURITY_ATTRIBUTES sa{sizeof(sa),nullptr,TRUE};
    HANDLE log=CreateFileW((output/L"worker.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ,&sa,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    HANDLE input=CreateFileW(L"NUL",GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE,&sa,OPEN_EXISTING,0,nullptr);
    STARTUPINFOW si{sizeof(si)}; si.dwFlags=STARTF_USESTDHANDLES; si.hStdOutput=si.hStdError=log; si.hStdInput=input;
    PROCESS_INFORMATION pi{};
    BOOL started=log!=INVALID_HANDLE_VALUE && input!=INVALID_HANDLE_VALUE && CreateProcessW(launchArgs[1].c_str(),command.data(),nullptr,nullptr,TRUE,CREATE_NO_WINDOW,nullptr,launchArgs[3].c_str(),&si,&pi);
    if(log!=INVALID_HANDLE_VALUE) CloseHandle(log); if(input!=INVALID_HANDLE_VALUE) CloseHandle(input);
    if(started) { finished=false; EnableWindow(startButton,FALSE); EnableWindow(errorButton,FALSE); EnableWindow(bothButton,FALSE); SetTimer(hwnd,1,400,nullptr); child=pi.hProcess; CloseHandle(pi.hThread); }
    else { finished=true; KillTimer(hwnd,1); SetWindowTextW(statusLabel,L"Could not start analyzer. Check Python and output paths."); }
 }

int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,LPWSTR,int) {
    int count=0; auto argv=CommandLineToArgvW(GetCommandLineW(),&count);
    if(count!=4 && count!=5) { MessageBoxW(nullptr,L"Arguments: python.exe deep_analysis.py repository output-directory",L"Starlight analysis",MB_ICONERROR); if(argv) LocalFree(argv); return 2; }
    std::vector<std::wstring> args(argv,argv+count); LocalFree(argv);
    launchArgs=args;
    output=fs::path(args[3])/L"analysis";
    try { fs::create_directories(output); }
    catch(...) { MessageBoxW(nullptr,L"Cannot create output directory.",L"Starlight analysis",MB_ICONERROR); return 3; }
    if(fs::exists(output/L"byte-manifest.jsonl")) { MessageBoxW(nullptr,L"Use a new output directory. Existing evidence will not be overwritten.",L"Starlight analysis",MB_ICONERROR); return 4; }
    INITCOMMONCONTROLSEX cc{sizeof(cc),ICC_PROGRESS_CLASS}; InitCommonControlsEx(&cc);
    WNDCLASSW wc{}; wc.lpfnWndProc=procedure; wc.hInstance=instance; wc.lpszClassName=L"StarlightDeepAnalysis"; wc.hCursor=LoadCursor(nullptr,IDC_ARROW); wc.hbrBackground=audit_theme::brush(); RegisterClassW(&wc);
    HWND hwnd=CreateWindowW(wc.lpszClassName,L"Starlight Engine — Deep Analysis",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,772,480,nullptr,nullptr,instance,nullptr);
    if(!hwnd) return 5;
    ShowWindow(hwnd,SW_SHOWNORMAL); UpdateWindow(hwnd);
    if(count==5) startAnalysis(hwnd);
    MSG msg{}; while(GetMessageW(&msg,nullptr,0,0)>0) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    return 0;
}
