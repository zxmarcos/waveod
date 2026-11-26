@echo off
rem waveod Studio's new interface on the firmware in the emulator (no pedal).
rem The emulated pedal takes about a minute to start; the window says "conectando..." meanwhile.
rem The firmware and its ELF come in pairs: python tools\buildfw.py builds the last commit into
rem out\QME-200_waveod_grid.bin + .elf (the emulator runs the ELF beside the firmware).
rem Another firmware: waveod-studio-web.bat --emu ARQUIVO.bin (with ARQUIVO.elf beside it)
setlocal
set "FW=%~dp0..\out\QME-200_waveod_grid.bin"
if not exist "%FW%" (
    echo Firmware nao encontrado: %FW%
    echo Gere com: python tools\buildfw.py
    pause
    exit /b 1
)
if not exist "%~dp0..\out\QME-200_waveod_grid.elf" (
    echo Falta o ELF deste firmware: out\QME-200_waveod_grid.elf
    echo Gere os dois juntos com: python tools\buildfw.py
    pause
    exit /b 1
)
call "%~dp0waveod-studio-web.bat" --emu "%FW%" %*
