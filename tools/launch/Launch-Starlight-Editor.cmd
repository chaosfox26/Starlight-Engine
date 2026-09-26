@echo off
setlocal
if not exist "%~dp0..\..\out\classic\Release\Starlight.NativeEditor.exe" (
  echo Build the native editor first: python Build-Native.py --config Release --test --build-dir out/classic
  pause
  exit /b 1
)
start "" /D "%~dp0..\..\" "%~dp0..\..\out\classic\Release\Starlight.NativeEditor.exe" --show-terrain %*
