@echo off
cd /d "%~dp0"
set PYTHONUTF8=1
where pythonw.exe >nul 2>nul
if %errorlevel%==0 (
    start "" pythonw.exe ".\tools\handheld_control.pyw"
    exit /b
)

where pyw.exe >nul 2>nul
if %errorlevel%==0 (
    start "" pyw.exe ".\tools\handheld_control.pyw"
    exit /b
)

python ".\tools\handheld_control.pyw"
