@echo off
cd /d "%~dp0"
if not exist "build\web\site\index.html" (
    echo Primeiro execute build-web.ps1 para compilar a versao web.
    pause
    exit /b 1
)
echo.
echo Abra http://localhost:8080 no navegador e clique em Jogar.
echo Mantenha esta janela aberta enquanto estiver jogando.
echo Para encerrar o servidor, pressione Ctrl+C.
echo.
python -m http.server 8080 --directory build/web/site
pause
