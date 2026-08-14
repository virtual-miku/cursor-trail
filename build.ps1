$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $MyInvocation.MyCommand.Path
$projectPath = Join-Path $projectRoot "CursorTrail.vcxproj"
$outputPath = Join-Path $projectRoot "bin\CursorTrail.exe"
$vswherePath = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"

if (-not (Test-Path -LiteralPath $vswherePath)) {
    throw "Visual Studio Installer tidak ditemukan. Install Visual Studio Build Tools dengan workload C++."
}

$visualStudioPath = & $vswherePath `
    -latest `
    -products * `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -property installationPath

if (-not $visualStudioPath) {
    throw "MSVC C++ Build Tools tidak ditemukan. Install workload 'Desktop development with C++'."
}

$msbuildPath = Join-Path $visualStudioPath "MSBuild\Current\Bin\MSBuild.exe"
if (-not (Test-Path -LiteralPath $msbuildPath)) {
    throw "MSBuild tidak ditemukan di: $msbuildPath"
}

Write-Host "Building Cursor Trail (native C++ x64)..." -ForegroundColor Cyan
& $msbuildPath `
    $projectPath `
    /nologo `
    /m `
    /restore `
    /p:Configuration=Release `
    /p:Platform=x64 `
    /v:minimal

if ($LASTEXITCODE -ne 0) {
    throw "Build gagal dengan exit code $LASTEXITCODE."
}

if (-not (Test-Path -LiteralPath $outputPath)) {
    throw "Build selesai tanpa menghasilkan $outputPath."
}

$sizeKb = [math]::Round((Get-Item -LiteralPath $outputPath).Length / 1KB, 1)
Write-Host "Built: $outputPath ($sizeKb KB)" -ForegroundColor Green
