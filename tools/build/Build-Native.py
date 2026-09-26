"""One native CMake/Ninja entry point. Python orchestrates; no dotnet or MSBuild."""
import argparse
import os
from pathlib import Path
import subprocess
import shutil

root = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--config', choices=['Debug', 'Release'], default='Release')
parser.add_argument('--test', action='store_true')
parser.add_argument('--ngx-sdk', type=Path, default=os.environ.get('STARLIGHT_NGX_SDK'), help='Separately installed NGX SDK (or STARLIGHT_NGX_SDK environment variable)')
parser.add_argument('--build-dir', type=Path, help='Isolated native build directory; leaves a running editor build untouched')
args = parser.parse_args()
env = os.environ.copy()
if not shutil.which('cl.exe'):
    vswhere = Path(os.environ.get('ProgramFiles(x86)', r'C:\Program Files (x86)'))/'Microsoft Visual Studio/Installer/vswhere.exe'
    install = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    if not install:
        raise SystemExit('Install Visual Studio C++ build tools and the Windows SDK.')
    devcmd = Path(install)/'Common7/Tools/VsDevCmd.bat'
    captured = subprocess.check_output(f'cmd.exe /d /s /c ""{devcmd}" -arch=x64 -host_arch=x64 >nul && set"', text=True)
    for line in captured.splitlines():
        if '=' in line and not line.startswith('='):
            key, value = line.split('=', 1)
            env[key] = value
    ninja = Path(install)/'Common7/IDE/CommonExtensions/Microsoft/CMake/Ninja'
    env['PATH'] = str(ninja) + os.pathsep + env.get('Path', env.get('PATH', ''))
commands = [
    ['cmake', '--preset', 'native'],
    ['cmake', '--build', '--preset', 'native-'+args.config.lower()],
] + ([['ctest', '--preset', 'native-'+args.config.lower()]] if args.test else [])
if args.build_dir:
    build = str(args.build_dir.resolve())
    commands = [['cmake', '--preset', 'native', '-B', build],
                ['cmake', '--build', build, '--config', args.config, '-j', '12']]
    if args.test: commands.append(['ctest', '--test-dir', build, '-C', args.config, '--output-on-failure'])
if args.ngx_sdk:
    commands[0].append('-DSTARLIGHT_NGX_SDK=' + str(Path(args.ngx_sdk).resolve()))
for command in commands:
    subprocess.run(command, cwd=root, env=env, check=True)
