#pragma once
#include <windows.h>
namespace starlight::editor {
// One startup activation, never a timer or an always-on-top policy. The user
// remains free to activate another application immediately afterwards.
inline bool activate_startup_window(HWND window) {
    ShowWindow(window,IsIconic(window)?SW_RESTORE:SW_SHOW);
    const DWORD ownThread=GetCurrentThreadId();
    const HWND foreground=GetForegroundWindow();
    const DWORD foregroundThread=foreground?GetWindowThreadProcessId(foreground,nullptr):0;
    struct InputAttachment {
        DWORD own,other;bool attached;
        ~InputAttachment(){if(attached)AttachThreadInput(own,other,FALSE);}
    } attachment{ownThread,foregroundThread,foregroundThread && foregroundThread!=ownThread &&
        AttachThreadInput(ownThread,foregroundThread,TRUE)!=FALSE};
    SetWindowPos(window,HWND_TOP,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_SHOWWINDOW);
    SetForegroundWindow(window);
    if(GetForegroundWindow()==window){SetActiveWindow(window);SetFocus(window);return true;}
    return false;
}
}
