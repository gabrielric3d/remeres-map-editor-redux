@echo off
REM ============================================================
REM  rme.bat
REM  Menu de utilidades do RME Redux:
REM    1. Compilar (build_windows.bat)
REM    2. Deploy Release (deploy_release.bat)
REM ============================================================

setlocal
set "ROOT=%~dp0"

:menu
cls
echo ========================================================
echo   RME Redux - Menu
echo ========================================================
echo.
echo   [1] Compilar         (build_windows.bat)
echo   [2] Deploy Release   (deploy_release.bat)
echo   [3] Compilar + Deploy
echo.
echo   [0] Sair
echo.
set "OPC="
set /p "OPC=Escolha uma opcao: "

if "%OPC%"=="1" goto compilar
if "%OPC%"=="2" goto deploy
if "%OPC%"=="3" goto ambos
if "%OPC%"=="0" goto fim

echo.
echo   Opcao invalida.
timeout /t 2 >nul
goto menu

:compilar
echo.
call "%ROOT%build_windows.bat"
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
call "%ROOT%build_windows.bat"
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

:fim
endlocal
