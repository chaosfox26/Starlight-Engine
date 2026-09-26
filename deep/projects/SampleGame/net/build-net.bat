@echo off
REM Build the standalone net module test — Visual Studio 2026 toolchain (via vswhere).
REM Compiles test_net.cpp + ws_client.cpp, links ws2_32.lib. Console subsystem.
setlocal
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -prerelease -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set VSPATH=%%i
if "%VSPATH%"=="" ( echo [build-net] Visual Studio with C++ tools not found via vswhere & exit /b 1 )
echo [build-net] Using VS at: %VSPATH%
call "%VSPATH%\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 ( echo [build-net] vcvars64 failed & exit /b 1 )
if not exist "%~dp0bin" mkdir "%~dp0bin"
cl /nologo /std:c++17 /EHsc /W3 /O2 /DUNICODE /D_UNICODE ^
   "%~dp0test_net.cpp" "%~dp0ws_client.cpp" ^
   /Fe:"%~dp0bin\test_net.exe" /Fo:"%~dp0bin\\" ^
   /link ws2_32.lib
if errorlevel 1 ( echo [build-net] COMPILE FAILED & exit /b 1 )
echo [build-net] OK -^> %~dp0bin\test_net.exe
endlocal
