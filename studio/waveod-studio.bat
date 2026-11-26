@echo off
rem waveod Studio: the PC app of the waveod firmware.
rem Arguments go to the app, e.g.  waveod-studio.bat --emu ..\out\QME-200_waveod.bin
rem Another Python: set WAVEOD_PYTHON=C:\caminho\python.exe before running.
setlocal
cd /d "%~dp0"

set "PY=%WAVEOD_PYTHON%"
if not defined PY (
    rem the newest Python 3.10+ installed (find-python.ps1)
    for /f "usebackq delims=" %%P in (`powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0find-python.ps1"`) do set "PY="%%P""
)
if not defined PY (
    echo Python 3.10 ou mais novo nao foi encontrado.
    echo Instale de python.org marcando "Add python.exe to PATH", ou defina WAVEOD_PYTHON.
    pause
    exit /b 1
)

%PY% -m waveod_studio %*
if errorlevel 1 pause