@echo off
rem Usage: run.cmd analyze_track ^<args^>   or   run.cmd tag_samples ^<args^>
rem Runs the tool in the analysis venv. stdout carries exactly one JSON object; logs go to stderr.
setlocal
set "PY=%~dp0.venv\Scripts\python.exe"
set "PYTHONIOENCODING=utf-8"
set "PYTHONUTF8=1"
set "PYTHONWARNINGS=ignore"
if not exist "%PY%" (
  echo {"error": "analysis venv missing - run analysis/setup.cmd"}
  exit /b 2
)
"%PY%" "%~dp0cli.py" %*
exit /b %ERRORLEVEL%
