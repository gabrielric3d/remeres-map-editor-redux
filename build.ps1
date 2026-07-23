# build.ps1 — Build do RME Redux com CPU sob controle.
#
# Roda o cmake --build com prioridade BelowNormal para o processo e todos os
# filhos (cl.exe, link.exe), mantendo o PC responsivo durante a compilação.
#
# Uso:
#   .\build.ps1                      # preset ninja-release (padrão)
#   .\build.ps1 -Preset vs2022-release
#   .\build.ps1 -Jobs 16             # menos jobs = ainda mais leve
#   .\build.ps1 -Configure           # roda o configure antes do build

param(
    [string]$Preset = "ninja-release",
    [int]$Jobs = 24,
    [switch]$Configure
)

$ErrorActionPreference = "Stop"

# Mapear buildPreset -> configurePreset (para -Configure)
$configureMap = @{
    "ninja-release"  = "ninja-vcpkg"
    "ninja-debug"    = "ninja-vcpkg"
    "vs2022-release" = "vs2022-vcpkg"
    "vs2022-debug"   = "vs2022-vcpkg"
}

if ($Configure) {
    $cfgPreset = $configureMap[$Preset]
    if (-not $cfgPreset) { throw "Preset desconhecido: $Preset" }
    Write-Host "== Configure ($cfgPreset) ==" -ForegroundColor Cyan
    cmake --preset $cfgPreset
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

Write-Host "== Build ($Preset, $Jobs jobs, prioridade BelowNormal) ==" -ForegroundColor Cyan

# Inicia o build já com prioridade baixa; filhos (ninja/msbuild/cl/link) herdam.
$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = "cmake"
$psi.Arguments = "--build --preset $Preset --parallel $Jobs"
$psi.UseShellExecute = $false
$proc = [System.Diagnostics.Process]::Start($psi)
try { $proc.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal } catch {}

# Rede de segurança: rebaixa cl.exe/link.exe que escapem da herança de prioridade.
while (-not $proc.HasExited) {
    Start-Sleep -Seconds 5
    foreach ($name in @("cl", "link", "MSBuild", "ninja")) {
        Get-Process -Name $name -ErrorAction SilentlyContinue | ForEach-Object {
            try {
                if ($_.PriorityClass -ne [System.Diagnostics.ProcessPriorityClass]::BelowNormal) {
                    $_.PriorityClass = [System.Diagnostics.ProcessPriorityClass]::BelowNormal
                }
            } catch {}
        }
    }
}

exit $proc.ExitCode
