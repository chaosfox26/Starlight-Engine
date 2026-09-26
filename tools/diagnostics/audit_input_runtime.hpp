#pragma once
#include <DispatcherQueue.h>
#include <wrl/client.h>
// UI-churn diagnostics explicitly initialize the Windows queue which TSF can
// otherwise create late during control destruction. No windows are excluded
// from the measured inventory, and the application itself is unchanged.
struct AuditInputRuntime {
    Microsoft::WRL::ComPtr<ABI::Windows::System::IDispatcherQueueController> queue;
    HMODULE module = nullptr;
    AuditInputRuntime() {
        for (HWND h=nullptr;(h=FindWindowExW(HWND_MESSAGE,h,L"SystemUserAdapterWindowClass",nullptr))!=nullptr;)
            if (GetWindowThreadProcessId(h,nullptr)==GetCurrentThreadId()) return;
        module=LoadLibraryExW(L"CoreMessaging.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        using Create=HRESULT(WINAPI*)(DispatcherQueueOptions,PDISPATCHERQUEUECONTROLLER*);
        auto create=module?reinterpret_cast<Create>(GetProcAddress(module,"CreateDispatcherQueueController")):nullptr;
        DispatcherQueueOptions options{sizeof(options),DQTYPE_THREAD_CURRENT,DQTAT_COM_NONE};
        if(!create || FAILED(create(options,queue.GetAddressOf())))
            throw std::runtime_error("Cannot initialize isolated UI input runtime");
    }
    bool finish() {
        if(!queue)return true;
        Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncAction> action;
        Microsoft::WRL::ComPtr<ABI::Windows::Foundation::IAsyncInfo> info;
        bool okay=SUCCEEDED(queue->ShutdownQueueAsync(&action)) && SUCCEEDED(action.As(&info));
        auto deadline=GetTickCount64()+5000;
        AsyncStatus status=AsyncStatus::Started;
        while(okay && GetTickCount64()<deadline) {
            okay=SUCCEEDED(info->get_Status(&status));
            if(!okay || status!=AsyncStatus::Started)break;
            MSG message{};
            while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) {
                if(message.message!=WM_QUIT){TranslateMessage(&message);DispatchMessageW(&message);}
            }
            MsgWaitForMultipleObjects(0,nullptr,FALSE,1,QS_ALLINPUT);
        }
        queue.Reset();return okay && status==AsyncStatus::Completed;
    }
    ~AuditInputRuntime(){finish();if(module)FreeLibrary(module);}
};
