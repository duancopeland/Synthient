@echo off
rem Downloads (or finishes downloading) Synthient's instrument library: Surge XT, Dexed, OB-Xf,
rem the VSCO orchestra, Salamander piano, Karoryfer bass and guitar, and the General MIDI sounds.
rem Parts that are already installed are skipped. Options: --list, --only surge,vsco, --skip vsco
setlocal
set "PY=%~dp0analysis\python\python.exe"
if not exist "%PY%" (
  echo Synthient's Python is missing: %PY%
  echo Reinstall Synthient, or run library\tools\get_library.py with Python 3.8+ that has numpy and soundfile.
  pause
  exit /b 1
)
"%PY%" "%~dp0library\tools\get_library.py" %*
set "RC=%ERRORLEVEL%"
echo.
pause
exit /b %RC%
