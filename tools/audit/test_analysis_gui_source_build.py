import ctypes as c, ctypes.wintypes as w, json, subprocess, time, uuid
from pathlib import Path
u=c.WinDLL('user32',use_last_error=True)
u.SendMessageW.argtypes=[w.HWND,w.UINT,w.WPARAM,w.LPARAM];u.SendMessageW.restype=c.c_ssize_t
u.GetDlgItem.argtypes=[w.HWND,c.c_int];u.GetDlgItem.restype=w.HWND
u.SetWindowTextW.argtypes=[w.HWND,w.LPCWSTR]
u.GetWindowThreadProcessId.argtypes=[w.HWND,c.POINTER(w.DWORD)]
u.IsWindowEnabled.argtypes=[w.HWND];u.IsWindowEnabled.restype=w.BOOL
callback=c.WINFUNCTYPE(w.BOOL,w.HWND,w.LPARAM);u.EnumWindows.argtypes=[callback,w.LPARAM]
root=Path.cwd();fixture=root/'out'/('gui-source-build-'+uuid.uuid4().hex[:8]);fixture.mkdir()
(fixture/'tools/audit').mkdir(parents=True)
plan={'format':'starlight-runtime-checks-v1','profile':'prebuilt-runtime','stages':[{'name':'fixture-missing-check','executable':'MissingCheck.exe','arguments':[],'features':[],'timeoutSeconds':1}]}
(fixture/'tools/audit/runtime-checks.json').write_text(json.dumps(plan))
(fixture/'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.25)\nproject(Fixture LANGUAGES CXX)\n')
manifest=fixture/'toolchain with spaces \u03a9.json';manifest.write_text('{"format":"invalid-on-purpose"}')
p=subprocess.Popen([str(root/'out/native-analysis-gui/Starlight.DeepAnalysis.exe'),'--native',str(root/'out/classic/Release/Starlight.NativeAudit.exe'),str(fixture)])
def wait_for(fn,seconds=20):
    end=time.monotonic()+seconds
    while time.monotonic()<end:
        value=fn()
        if value:return value
        time.sleep(.1)
    raise TimeoutError('GUI fixture deadline')
def window():
    found=[]
    @callback
    def visit(h,l):
        pid=w.DWORD();u.GetWindowThreadProcessId(h,c.byref(pid))
        if pid.value==p.pid and u.GetDlgItem(h,107):found.append(h)
        return True
    u.EnumWindows(visit,0)
    return found[0] if found else None
results={}
try:
    hwnd=wait_for(window);check=u.GetDlgItem(hwnd,107);edit=u.GetDlgItem(hwnd,108)
    u.SendMessageW(check,0xF1,1,0);assert u.SendMessageW(check,0xF0,0,0)==1
    u.SendMessageW(hwnd,0x111,104,0)
    assert not list((fixture/'analysis').glob('run-*'))
    results['missingToolchainStartsNothing']=True
    text=c.create_unicode_buffer(str(manifest));u.SendMessageW(edit,0xC,0,c.addressof(text));u.SendMessageW(hwnd,0x111,104,0)
    summary=wait_for(lambda:next((fixture/'analysis').glob('run-*/audit-summary.json'),None))
    data=json.loads(summary.read_text());assert data['sourceBuildExecuted'] and not data['runtimeExecuted']
    assert data['stages'][0]['name']=='build' and data['stages'][0]['outcome']=='fail'
    assert data['stages'][1]['outcome']=='not-run'
    wait_for(lambda:u.IsWindowEnabled(check));results['sourceBuildForwardedAndFailureStopsRuntime']=True
    u.SendMessageW(check,0xF1,0,0);u.SendMessageW(hwnd,0x111,104,0)
    second=wait_for(lambda:next((x for x in (fixture/'analysis').glob('run-*/audit-summary.json') if x!=summary),None))
    data=json.loads(second.read_text());assert not data['sourceBuildExecuted'] and data['executionProfile']=='prebuilt-runtime'
    wait_for(lambda:u.IsWindowEnabled(check));results['uncheckedUsesPrebuiltRuntime']=True
    (fixture/'verification.json').write_text(json.dumps(results,indent=2))
    print(json.dumps({'evidence':str(fixture/'verification.json'),'checks':results}))
finally:
    if p.poll() is None:
        h=window()
        if h:u.SendMessageW(h,0x10,0,0)
        try:p.wait(timeout=5)
        except subprocess.TimeoutExpired:p.terminate();p.wait()
