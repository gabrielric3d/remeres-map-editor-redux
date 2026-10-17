@echo off
REM ============================================================
REM  gerar_pacote_mapper.bat
REM  Gera o RME Redux zipado, pronto para mandar a um mapper
REM  freelancer: so o executavel, as DLLs e os dados que o editor
REM  le em runtime. Fica de fora o que e de desenvolvimento
REM  (source, build, .git, scripts Python) e o que e pessoal
REM  (config.toml com caminhos locais e token do MCP, tokens do
REM  Discord, mapas, videos).
REM
REM  Puxa direto do workspace do Black Talon:
REM    - cliente : Tibia.dat, Tibia.otfi, Tibia.cat + sprites\
REM    - servidor: items.otb, items.xml, creatures_table.json
REM
REM  Saida: dist\RME_Redux_BlackTalon\              (pasta montada)
REM         dist\RME_Redux_BlackTalon_AAAA-MM-DD.zip (o que vai pro mapper)
REM
REM  Uso: gerar_pacote_mapper.bat [/nopause]
REM ============================================================

setlocal EnableExtensions

REM Raiz = pasta deste .bat, sem a barra final (robocopy se perde com \")
set "ROOT=%~dp0"
set "ROOT=%ROOT:~0,-1%"

REM --- Origens: ajuste aqui se o workspace mudar de lugar ---
set "BT=B:\Github\blacktalon-workspace"
set "CLIENT_THINGS=%BT%\client\data\things\1098"
set "SERVER_ITEMS=%BT%\server\data\items"
set "CREATURES_JSON=%BT%\server\data\scripts\creatures_table.json"

REM Nome do cliente que o pacote configura (e o que o mapper ve no editor)
set "CLIENT_NAME=Black Talon 10.98"

set "PKG_NAME=RME_Redux_BlackTalon"
set "OUT=%ROOT%\dist"
set "STAGE=%OUT%\%PKG_NAME%"

for /f "usebackq delims=" %%D in (`powershell -NoProfile -Command "Get-Date -Format yyyy-MM-dd"`) do set "STAMP=%%D"
set "ZIP=%OUT%\%PKG_NAME%_%STAMP%.zip"

echo ========================================================
echo   Pacote do mapper - RME Redux Black Talon
echo ========================================================
echo   Cliente : %CLIENT_THINGS%
echo   Servidor: %SERVER_ITEMS%
echo   Saida   : %ZIP%
echo.

REM ---------- Conferir as origens ----------
set "FALTA="
call :exige "%ROOT%\rme.exe"
call :exige "%ROOT%\data\menubar.xml"
call :exige "%ROOT%\data\1098\materials.xml"
call :exige "%CLIENT_THINGS%\Tibia.dat"
call :exige "%CLIENT_THINGS%\Tibia.cat"
call :exige "%CLIENT_THINGS%\sprites"
call :exige "%SERVER_ITEMS%\items.otb"
call :exige "%SERVER_ITEMS%\items.xml"
if defined FALTA goto :falhou

REM O pacote leva o rme.exe da raiz (o que voce testa no dia a dia).
REM Se o build\Release for mais novo, provavelmente faltou o deploy.
if exist "%ROOT%\build\Release\rme.exe" (
    powershell -NoProfile -Command "if ((Get-Item (Join-Path $env:ROOT 'build\Release\rme.exe')).LastWriteTime -gt (Get-Item (Join-Path $env:ROOT 'rme.exe')).LastWriteTime) { exit 1 }"
    if errorlevel 1 (
        echo [AVISO] build\Release\rme.exe e mais novo que o rme.exe da raiz.
        echo         Rode o "Deploy Release" antes se quiser mandar o build novo.
        echo.
    )
)

REM ---------- 1. Pasta de montagem limpa ----------
echo [1/6] Limpando a pasta de montagem...
if exist "%STAGE%" rmdir /s /q "%STAGE%"
if exist "%STAGE%" (
    echo [ERRO] Nao consegui apagar %STAGE%
    echo        O rme.exe de dentro do dist esta aberto?
    goto :falhou
)
mkdir "%STAGE%"
if exist "%ZIP%" del /f /q "%ZIP%"

REM ---------- 2. Executavel, DLLs e runtime do VC++ ----------
echo [2/6] Executavel, DLLs e runtime do Visual C++...
call :copiar "%ROOT%" "%STAGE%" rme.exe *.dll
if errorlevel 1 goto :falhou

REM O exe e as DLLs do vcpkg dependem do runtime do VC++. Vai junto, ao lado
REM do exe, para o mapper nao precisar instalar o redistributable.
set "SYSDIR=%SystemRoot%\System32"
if exist "%SystemRoot%\Sysnative\" set "SYSDIR=%SystemRoot%\Sysnative"
for %%F in (msvcp140.dll msvcp140_atomic_wait.dll vcruntime140.dll vcruntime140_1.dll) do (
    if exist "%SYSDIR%\%%F" (
        copy /y "%SYSDIR%\%%F" "%STAGE%\%%F" >nul
    ) else (
        echo [AVISO] %%F nao encontrado em %SYSDIR% - o mapper vai precisar do VC++ Redistributable.
    )
)

REM ---------- 3. Interface, scripts Lua e dados do editor ----------
echo [3/6] Interface, scripts Lua e dados do editor...
call :copiar "%ROOT%\assets" "%STAGE%\assets" /E
if errorlevel 1 goto :falhou
REM br_map_setup e do Battle Royale; .py/__pycache__ sao ferramentas de dev
call :copiar "%ROOT%\scripts" "%STAGE%\scripts" /E /XD __pycache__ br_map_setup /XF *.py *.pyc
if errorlevel 1 goto :falhou
call :copiar "%ROOT%\data" "%STAGE%\data" menubar.xml
if errorlevel 1 goto :falhou
call :copiar "%ROOT%\data\forced_light" "%STAGE%\data\forced_light" /E
if errorlevel 1 goto :falhou
call :copiar "%ROOT%\data\presets" "%STAGE%\data\presets" /E
if errorlevel 1 goto :falhou
call :copiar "%ROOT%\data\shaders" "%STAGE%\data\shaders" /E
if errorlevel 1 goto :falhou
REM Brushes, tilesets, bordas etc. do 1098. O items.xml vem do servidor.
call :copiar "%ROOT%\data\1098" "%STAGE%\data\1098" *.xml /XF items.xml
if errorlevel 1 goto :falhou

REM ---------- 4. Assets do cliente ----------
echo [4/6] Assets do cliente Black Talon (Tibia.dat, Tibia.cat e sprites, ~1.2 GB)...
call :copiar "%CLIENT_THINGS%" "%STAGE%\data\1098" Tibia.dat Tibia.cat Tibia.otfi
if errorlevel 1 goto :falhou
REM O .cat aponta para "sprites\Tibia_N.spr" relativo a ele: o nome da pasta
REM tem de ser exatamente "sprites", ao lado do Tibia.cat.
call :copiar "%CLIENT_THINGS%\sprites" "%STAGE%\data\1098\sprites" /E
if errorlevel 1 goto :falhou

REM ---------- 5. Itens e criaturas do servidor ----------
echo [5/6] items.otb, items.xml e criaturas do servidor...
call :copiar "%SERVER_ITEMS%" "%STAGE%\data\1098" items.otb items.xml
if errorlevel 1 goto :falhou
if exist "%CREATURES_JSON%" (
    copy /y "%CREATURES_JSON%" "%STAGE%\data\creatures_table.json" >nul
) else (
    echo [AVISO] creatures_table.json nao encontrado - o mapper fica sem monstros na palette.
)

REM ---------- 6. Configuracao do cliente e zip ----------
echo [6/6] Configurando o cliente e zipando...

REM O parse do .dat escolhe o formato pela assinatura declarada no
REM clients.toml, entao ela tem de bater com os arquivos que foram no pacote.
REM Le direto deles: Tibia.dat guarda nos 4 primeiros bytes; o Tibia.cat
REM (catalogo SCAT) guarda no offset 8, depois do magic e da versao.
set "DAT_SIG="
set "SPR_SIG="
for /f "usebackq delims=" %%S in (`powershell -NoProfile -Command "'{0:X}' -f [BitConverter]::ToUInt32([IO.File]::ReadAllBytes((Join-Path $env:STAGE 'data\1098\Tibia.dat')), 0)"`) do set "DAT_SIG=%%S"
for /f "usebackq delims=" %%S in (`powershell -NoProfile -Command "'{0:X}' -f [BitConverter]::ToUInt32([IO.File]::ReadAllBytes((Join-Path $env:STAGE 'data\1098\Tibia.cat')), 8)"`) do set "SPR_SIG=%%S"
if not defined DAT_SIG (
    echo [ERRO] Nao consegui ler a assinatura do Tibia.dat.
    goto :falhou
)
if not defined SPR_SIG (
    echo [ERRO] Nao consegui ler a assinatura do Tibia.cat.
    goto :falhou
)
echo        Assinaturas: dat %DAT_SIG% / cat %SPR_SIG%

REM So o cliente do Black Talon, com caminhos relativos a data\1098. Os
REM valores de otb espelham o "10.98 (Copy)" do data\clients.toml.
> "%STAGE%\data\clients.toml" (
    echo # Gerado por gerar_pacote_mapper.bat: so o cliente do Black Talon.
    echo [[clients]]
    echo configType = 'dat_otb'
    echo datSignature = '%DAT_SIG%'
    echo dataDirectory = '1098'
    echo default = true
    echo description = 'Black Talon'
    echo extended = true
    echo frameDurations = true
    echo frameGroups = true
    echo metadataFile = 'Tibia.dat'
    echo name = '%CLIENT_NAME%'
    echo otbFile = 'items.otb'
    echo otbId = 57
    echo otbMajor = 3
    echo otbmVersions = [ 3 ]
    echo sprSignature = '%SPR_SIG%'
    echo spritesFile = 'Tibia.cat'
    echo transparency = true
    echo version = 1098
)

REM config.toml minimo: sem ele o editor pede a pasta dos assets na primeira
REM vez. Os caminhos sao relativos a pasta do rme.exe (o editor le o
REM config.toml do diretorio de trabalho, que e a pasta do exe).
REM A pasta de screenshots ja vai preenchida: vazia, o editor usaria a
REM Pictures do usuario, e um nome de usuario com acento corrompia o
REM config.toml nos exes anteriores ao fix de UTF-8 do Settings::save.
mkdir "%STAGE%\screenshots"
> "%STAGE%\config.toml" (
    echo # Configuracao inicial do pacote, gerada por gerar_pacote_mapper.bat.
    echo # O editor completa o resto sozinho.
    echo [editor]
    echo default_client_version = 57
    echo.
    echo [graphics]
    echo creatures_json_path = 'data\creatures_table.json'
    echo screenshot_directory = 'screenshots\'
    echo.
    echo [[clients]]
    echo clientPath = 'data\1098\'
    echo default = true
    echo name = '%CLIENT_NAME%'
)

if exist "%ROOT%\tools\pacote_mapper\LEIA-ME.txt" copy /y "%ROOT%\tools\pacote_mapper\LEIA-ME.txt" "%STAGE%\LEIA-ME.txt" >nul

echo        Zipando - pode levar alguns minutos...
powershell -NoProfile -Command "try { Add-Type -AssemblyName System.IO.Compression.FileSystem; [IO.Compression.ZipFile]::CreateFromDirectory($env:STAGE, $env:ZIP, [IO.Compression.CompressionLevel]::Optimal, $true) } catch { Write-Host $_.Exception.Message; exit 1 }"
if errorlevel 1 (
    echo [ERRO] Falha ao gerar o zip.
    goto :falhou
)

set "ZIP_MB=?"
for /f "usebackq delims=" %%M in (`powershell -NoProfile -Command "'{0:N0}' -f ((Get-Item $env:ZIP).Length / 1MB)"`) do set "ZIP_MB=%%M"

echo.
echo ========================================================
echo   Pronto: %ZIP%
echo   Tamanho: %ZIP_MB% MB
echo ========================================================
echo   A pasta dist\%PKG_NAME% fica para voce testar antes de
echo   mandar: abra o rme.exe de la e carregue um mapa.
echo.
explorer /select,"%ZIP%"
if /i not "%~1"=="/nopause" pause
endlocal
exit /b 0

:falhou
echo.
echo [ERRO] O pacote NAO foi gerado.
if /i not "%~1"=="/nopause" pause
endlocal
exit /b 1

REM ------------------------------------------------------------
REM  Subrotinas
REM ------------------------------------------------------------

:exige
if exist "%~1" exit /b 0
echo [ERRO] Nao encontrado: %~1
set "FALTA=1"
exit /b 0

:copiar
REM robocopy: 0-7 = sucesso (copiou, nada a copiar, extras...); 8+ = falha.
REM A saida vai para um log e so aparece se der erro.
robocopy %* /NFL /NDL /NJH /NJS /NP /R:2 /W:1 > "%TEMP%\rme_pacote_robocopy.log"
if errorlevel 8 (
    type "%TEMP%\rme_pacote_robocopy.log"
    echo [ERRO] robocopy falhou: %*
    exit /b 1
)
exit /b 0
