"""Developer-only build of the native portable/install launcher."""
import os, subprocess, shutil, tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
out=root/'out/launcher';out.mkdir(parents=True,exist_ok=True)
vswhere=Path(os.environ['ProgramFiles(x86)'])/'Microsoft Visual Studio/Installer/vswhere.exe'
vs=Path(subprocess.check_output([str(vswhere),'-latest','-products','*','-requires','Microsoft.VisualStudio.Component.VC.Tools.x86.x64','-property','installationPath'],text=True).strip())
dev=vs/'Common7/Tools/VsDevCmd.bat'
block=subprocess.check_output(f'cmd.exe /d /s /c ""{dev}" -arch=x64 -host_arch=x64 >nul && set"',text=True)
env=os.environ.copy()
for line in block.splitlines():
 if '=' in line and not line.startswith('='):
  k,v=line.split('=',1);env[k]=v
compiler=sorted((vs/'VC/Tools/MSVC').glob('*/bin/Hostx64/x64/cl.exe'))[-1]
rc=shutil.which('rc.exe',path=env.get('PATH',env.get('Path','')))
if not rc: rc=str(sorted((Path(os.environ['ProgramFiles(x86)'])/'Windows Kits/10/bin').glob('*/x64/rc.exe'))[-1])
resource=out/'launcher.rc'
resource.write_text('103 RCDATA \"'+(root/'design/branding/starlight-setup-nebula-v1.png').as_posix()+'\"\n102 RCDATA \"'+(root/'design/branding/starlight-glass-icon-v1.png').as_posix()+'\"\n101 ICON "'+(root/'design/branding/starlight-editor.ico').as_posix()+'"\n',encoding='utf-8')
with (out/'build.log').open('w') as log:
 subprocess.run([rc,'/nologo','/fo',str(out/'launcher.res'),str(resource)],env=env,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
 subprocess.run([str(compiler),'/nologo','/std:c++20','/EHsc','/O2','/MT','/utf-8',str(root/'tools/packaging/launcher.cpp'),str(out/'launcher.res'),'/Fe:'+str(out/'Starlight.exe'),'/Fo:'+str(out/'launcher.obj'),'/link','/SUBSYSTEM:WINDOWS','/MANIFEST:EMBED','user32.lib','shell32.lib','comctl32.lib','ole32.lib','advapi32.lib','bcrypt.lib','uuid.lib', 'gdiplus.lib', 'dwmapi.lib', 'gdi32.lib'],env=env,cwd=out,stdout=log,stderr=subprocess.STDOUT,check=True)
subprocess.run([str(out/'Starlight.exe'),'--self-test'],check=True)
mt=Path(rc).with_name('mt.exe')
embedded=out/'embedded-manifest.xml'
subprocess.run([str(mt),'-nologo','-inputresource:'+str(out/'Starlight.exe')+';#1','-out:'+str(embedded)],check=True,capture_output=True)
assert 'Microsoft.Windows.Common-Controls' in embedded.read_text(encoding='utf-8-sig')
# Run an executable-only copy: sidecar manifests must not hide packaging errors.
with tempfile.TemporaryDirectory(prefix='starlight-launcher-isolated-') as isolated:
 isolated_exe=Path(isolated)/'Starlight.exe'
 shutil.copy2(out/'Starlight.exe',isolated_exe)
 subprocess.run([str(isolated_exe),'--self-test'],check=True,timeout=60)
print('Native launcher built; embedded manifest and isolated lifecycle checks passed.')
