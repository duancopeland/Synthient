@echo off
rem Double-click to install Synthient for the current user (no admin needed).
rem Installs everything: app, instruments, synths, analysis and stem tools. Asks where to install and for your API key.
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0install.ps1" %*
set "RC=%ERRORLEVEL%"
echo.
if not "%RC%"=="0" echo Installation failed (exit code %RC%).
pause
exit /b %RC%
