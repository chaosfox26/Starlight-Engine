"""Compile the progress GUI independently of the engine; optionally launch detached."""
import argparse, datetime, os, shutil, subprocess, sys
from pathlib import Path

def build(destination):
    root=Path(__file__).resolve().parents[2]
    destination.mkdir(parents=True,exist_ok=True)
    env=os.environ.copy()
    vswhere=Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
    install=Path(subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip())
    dev=install/'Common7/Tools/VsDevCmd.bat'
    block=subprocess.check_output(f'cmd.exe /d /s /c ""{dev}" -arch=x64 -host_arch=x64 >nul && set"',text=True)
    for line in block.splitlines():
        if '=' in line and not line.startswith('='):
            k,v=line.split('=',1);env[k]=v
    compiler=sorted((install/'VC/Tools/MSVC').glob('*/bin/Hostx64/x64/cl.exe'))[-1]
    exe=destination/'Starlight.DeepAnalysis.exe'
    with (destination/'build.log').open('w') as log:
        subprocess.run([str(compiler),'/nologo','/std:c++20','/EHsc','/O2','/W4','/utf-8',str(root/'tools/audit/deep_analysis_gui.cpp'),'/Fe:'+str(exe),'/Fo:'+str(destination/'deep_analysis_gui.obj'),'/link','/SUBSYSTEM:WINDOWS','user32.lib','shell32.lib','comctl32.lib','gdi32.lib'],env=env,cwd=destination,stdout=log,stderr=subprocess.STDOUT,check=True)
    return root,exe

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--build-dir',type=Path,required=True);p.add_argument('--launch',action='store_true');p.add_argument('--output',type=Path);a=p.parse_args()
    root,exe=build(a.build_dir.resolve());print('Built:',exe)
    if a.launch:
        out=(a.output or root/'analysis'/datetime.datetime.now().strftime('run-%Y%m%d-%H%M%S')).resolve()
        # The reusable GUI snapshots all reporting modules into each fresh run.
        proc=subprocess.Popen([str(exe),sys.executable,str(root/'tools/audit/deep_analysis.py'),str(root),str(out)],cwd=root,creationflags=subprocess.DETACHED_PROCESS|subprocess.CREATE_NEW_PROCESS_GROUP,close_fds=True)
        print('Detached progress GUI PID:',proc.pid);print('Output:',out)
        # Deliberately no wait, poll, stream or benchmark launch.
