@echo off
rem Executa o prologo (Richter vs Dracula) nativamente, sem emulador de PS1.
rem Deve ser executado a partir da pasta raiz do sotn-decomp (o cd abaixo garante isso).
cd /d "%~dp0"
"build\pc\sotn.exe" --stage st0 --player ric --disk "disks\sotn.us.cue"
rem Mantem a janela aberta pra dar pra ler crash/erro em vez de so desaparecer.
pause
