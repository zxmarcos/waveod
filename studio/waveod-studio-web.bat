@echo off
rem waveod Studio, the new interface (GRID screen): the page of ui\ in a pywebview window.
rem Arguments go to the app, e.g.
rem     waveod-studio-web.bat                                   the pedal on USB
rem     waveod-studio-web.bat --emu ..\out\QME-200_waveod.bin   the firmware in the emulator
rem The page is built again (npm run build) when a file of ui\src is newer than the build.
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

rem pywebview: the window
%PY% -c "import webview" >nul 2>nul
if errorlevel 1 (
    echo A interface nova precisa do pywebview. Instalando...
    %PY% -m pip install "pywebview>=6.2"
    if errorlevel 1 (
        echo Nao foi possivel instalar o pywebview.
        pause
        exit /b 1
    )
)

rem the page: built when missing or older than its sources
set "BUILD="
if not exist "waveod_studio\web\index.html" set "BUILD=1"
if not defined BUILD (
    powershell -NoProfile -Command "$b = (Get-Item 'waveod_studio\web\index.html').LastWriteTime; if (Get-ChildItem 'ui\src','ui\index.html','ui\package.json' -Recurse -File | Where-Object { $_.LastWriteTime -gt $b }) { exit 1 }" >nul 2>nul
    if errorlevel 1 set "BUILD=1"
)
if defined BUILD (
    where npm >nul 2>nul
    if errorlevel 1 (
        if not exist "waveod_studio\web\index.html" (
            echo A pagina ainda nao foi gerada e o Node.js nao foi encontrado.
            echo Instale o Node.js 20 ou mais novo de nodejs.org e rode de novo.
            pause
            exit /b 1
        )
        echo Node.js nao encontrado: usando a pagina gerada antes.
    ) else (
        echo Gerando a pagina...
        pushd ui
        if not exist "node_modules" call npm install --no-fund --no-audit
        call npm run build --silent
        if errorlevel 1 echo Falha ao gerar a pagina: usando a anterior, se houver.
        popd
        if not exist "waveod_studio\web\index.html" (
            echo Falha ao gerar a pagina.
            pause
            exit /b 1
        )
    )
)

%PY% -m waveod_studio --web %*
if errorlevel 1 pause
