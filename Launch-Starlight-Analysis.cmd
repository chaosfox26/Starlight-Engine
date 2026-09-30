@echo off
python "%~dp0tools\launch\launch_analysis.py"
if errorlevel 1 pause
