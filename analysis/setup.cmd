@echo off
rem Recreates analysis\.venv from the pinned requirements.
rem   setup.cmd               core: librosa analysis + sample tagging + Basic Pitch (ONNX)
rem   setup.cmd --with-stems  core + CPU torch + Demucs (about 1 GB more)
rem Python: uses %CLAUDEDAW_PYTHON% if set, else a Python 3.12/3.11 from the py launcher,
rem else provisions 3.12 with uv (installed into the system Python with pip if missing).
setlocal EnableDelayedExpansion
set "HERE=%~dp0"
set "VENV=%HERE%.venv"
set "WITH_STEMS=0"
if /i "%~1"=="--with-stems" set "WITH_STEMS=1"

set "PYEXE="
if defined CLAUDEDAW_PYTHON set "PYEXE=%CLAUDEDAW_PYTHON%"
if not defined PYEXE for %%V in (3.12 3.11 V:Astral/CPython3.12 V:Astral/CPython3.11) do (
  if not defined PYEXE (
    for /f "delims=" %%P in ('py -%%V -c "import sys;print(sys.executable)" 2^>nul') do set "PYEXE=%%P"
  )
)
if not defined PYEXE (
  echo No Python 3.11/3.12 found - provisioning 3.12 with uv...
  python -m uv --version >nul 2>&1 || python -m pip install --user uv || goto :fail
  python -m uv python install 3.12 || goto :fail
  for /f "delims=" %%P in ('python -m uv python find 3.12') do set "PYEXE=%%P"
)
if not defined PYEXE goto :fail
echo Using Python: %PYEXE%

if exist "%VENV%" (
  echo Removing old venv...
  rmdir /s /q "%VENV%" || goto :fail
)
"%PYEXE%" -m venv "%VENV%" || goto :fail
set "VPY=%VENV%\Scripts\python.exe"
"%VPY%" -m pip install --upgrade pip || goto :fail
"%VPY%" -m pip install -r "%HERE%requirements.txt" || goto :fail
"%VPY%" -m pip install --no-deps -r "%HERE%requirements-basic-pitch.txt" || goto :fail
if "%WITH_STEMS%"=="1" (
  "%VPY%" -m pip install -r "%HERE%requirements-stems.txt" || goto :fail
)
"%VPY%" -c "import librosa, basic_pitch, onnxruntime; print('core OK, librosa', librosa.__version__)" || goto :fail
if "%WITH_STEMS%"=="1" (
  "%VPY%" -c "import torch, demucs; print('stems OK, torch', torch.__version__, 'cuda', torch.cuda.is_available())" || goto :fail
)
echo Done. Try: "%HERE%run.cmd" analyze_track some.wav
exit /b 0

:fail
echo setup failed 1>&2
exit /b 1
