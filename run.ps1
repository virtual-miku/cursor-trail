param(
    [switch]$NoBuild
)

$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$buildScript = Join-Path $projectRoot "build.ps1"
$appPath = Join-Path $projectRoot "bin\CursorTrail.exe"

if (-not $NoBuild -or -not (Test-Path -LiteralPath $appPath)) {
    & $buildScript
}

Start-Process -FilePath $appPath -WorkingDirectory $projectRoot
Write-Host "Cursor Trail started. Klik kanan ikon tray untuk pengaturan; klik ganda untuk pause/resume."
