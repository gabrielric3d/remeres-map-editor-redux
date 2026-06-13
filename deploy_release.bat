@echo off
REM ============================================================
REM  deploy_release.bat
REM  Copia todos os arquivos de build\Release (exceto a pasta
REM  "assets") para a raiz do projeto rme_redux, sobrescrevendo.
REM ============================================================

setlocal

REM Raiz = pasta onde este .bat esta localizado
set "ROOT=%~dp0"
set "SOURCE=%ROOT%build\Release"
REM Remove a barra invertida final de ROOT para evitar quebrar as aspas
set "DEST=%ROOT:~0,-1%"

echo Origem : %SOURCE%
echo Destino: %DEST%
echo.

if not exist "%SOURCE%" (
    echo [ERRO] Pasta de origem nao encontrada: %SOURCE%
    pause
    exit /b 1
)

REM robocopy copia somente os arquivos do nivel atual (sem subpastas).
REM /XD assets  -> exclui a pasta assets caso exista
REM /NFL /NDL   -> reduz o log de arquivos/diretorios
REM /NJH /NJS   -> sem cabecalho/sumario
robocopy "%SOURCE%" "%DEST%" /XD "%SOURCE%\assets" /NFL /NDL /NJH /NJS

REM robocopy: codigos < 8 indicam sucesso
if %ERRORLEVEL% GEQ 8 (
    echo.
    echo [ERRO] Falha ao copiar arquivos. Codigo: %ERRORLEVEL%
    pause
    exit /b 1
)

echo.
echo Concluido com sucesso.
endlocal
