@echo off
REM ============================================================
REM  rme.bat
REM  Menu de utilidades do RME Redux:
REM    1. Compilar (build_windows.bat)
REM    2. Deploy Release (deploy_release.bat)
REM ============================================================

setlocal
set "ROOT=%~dp0"

REM Jobs usados no modo economico (baixa memoria). Menos jobs = menor pico
REM de RAM/CPU, build um pouco mais lento. Ajuste aqui se precisar.
set "ECO_JOBS=2"

:menu
cls
echo ========================================================
echo   RME Redux - Menu
echo ========================================================
echo.
echo   [1] Compilar             (build_windows.bat)
echo   [2] Deploy Release       (deploy_release.bat)
echo   [3] Compilar + Deploy
echo   [4] Compilar economico   (baixa memoria - %ECO_JOBS% jobs)
echo.
echo   [0] Sair
echo.
set "OPC="
set /p "OPC=Escolha uma opcao: "

if "%OPC%"=="1" goto compilar
if "%OPC%"=="2" goto deploy
if "%OPC%"=="3" goto ambos
if "%OPC%"=="4" goto compilar_eco
if "%OPC%"=="0" goto fim

echo.
echo   Opcao invalida.
timeout /t 2 >nul
goto menu

:compilar
echo.
call :ask_jobs
call "%ROOT%build_windows.bat" %JOBS%
echo.
pause
goto menu

:compilar_eco
echo.
echo   Modo economico: %ECO_JOBS% jobs paralelos ^(menor uso de RAM/CPU^).
call "%ROOT%build_windows.bat" %ECO_JOBS%
echo.
pause
goto menu

:deploy
echo.
call "%ROOT%deploy_release.bat"
echo.
pause
goto menu

:ambos
echo.
call :ask_jobs
call "%ROOT%build_windows.bat" %JOBS%
if %ERRORLEVEL% neq 0 (
    echo.
    echo   [ERRO] Compilacao falhou. Deploy cancelado.
    echo.
    pause
    goto menu
)
echo.
call "%ROOT%deploy_release.bat"
echo.
pause
goto menu

:ask_jobs
REM Pergunta quantos jobs paralelos usar na compilacao.
REM Enter = padrao do build_windows.bat (4 compile / 24 vcpkg).
set "JOBS="
set /p "JOBS=Jobs paralelos [Enter = padrao, 4-16 recomendado]: "
if not defined JOBS goto :eof
REM Valida: apenas digitos
echo %JOBS%| findstr /r "^[0-9][0-9]*$" >nul
if errorlevel 1 (
    echo   Valor invalido "%JOBS%" - usando padrao.
    set "JOBS="
)
goto :eof

:fim
endlocal
