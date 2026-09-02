<#
.SYNOPSIS
    Packages AudioMasteringTool for Windows 1.0 Release.
.DESCRIPTION
    Builds the Release solution, runs the full 35-test CTest suite, stages the standalone
    distribution directory in dist/, and compiles the Inno Setup installer if iscc.exe is available.
#>

param(
    [string]$BuildDir = "build/win",
    [string]$DistDir = "dist",
    [switch]$SkipTests
)

$ErrorActionPreference = "Stop"

Write-Host "==================================================" -ForegroundColor Cyan
Write-Host " AudioMasteringTool 1.0 Windows Release Packager" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

# 1. Build MSVC Release
Write-Host "`n[1/4] Building MSVC Release..." -ForegroundColor Yellow
cmake --build $BuildDir --config Release
if ($LASTEXITCODE -ne 0) {
    Write-Error "Build failed with exit code $LASTEXITCODE."
}

# 2. Run Test Suite
if (-not $SkipTests) {
    Write-Host "`n[2/4] Executing full 35-test suite..." -ForegroundColor Yellow
    ctest --test-dir $BuildDir -C Release --output-on-failure
    if ($LASTEXITCODE -ne 0) {
        Write-Error "Test suite failed with exit code $LASTEXITCODE."
    }
} else {
    Write-Host "`n[2/4] Skipping tests (-SkipTests specified)." -ForegroundColor DarkGray
}

# 3. Stage Portable Release Folder
Write-Host "`n[3/4] Staging release distribution in $DistDir/AudioMasteringTool-1.0.0-win64..." -ForegroundColor Yellow
$StageDir = Join-Path $RepoRoot "$DistDir/AudioMasteringTool-1.0.0-win64"
if (Test-Path $StageDir) {
    Remove-Item -Recurse -Force $StageDir
}
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

Copy-Item (Join-Path $BuildDir "src/app/Release/audiomasteringtool.exe") $StageDir
Copy-Item (Join-Path $BuildDir "src/worker/Release/amt_worker.exe") $StageDir
Copy-Item (Join-Path $BuildDir "src/cli/Release/amt_cli.exe") $StageDir
Copy-Item -Recurse "models" $StageDir
Copy-Item -Recurse "docs" $StageDir
if (Test-Path "LICENSE") { Copy-Item "LICENSE" $StageDir }
if (Test-Path "README.md") { Copy-Item "README.md" $StageDir }

Write-Host "Staged binaries and assets to $StageDir" -ForegroundColor Green

# 4. Compile Inno Setup Installer if available
Write-Host "`n[4/4] Checking for Inno Setup Compiler (iscc.exe)..." -ForegroundColor Yellow
$InnoPaths = @(
    "iscc.exe",
    "C:\Program Files (x86)\Inno Setup 6\iscc.exe",
    "C:\Program Files\Inno Setup 6\iscc.exe"
)
$Iscc = $null
foreach ($path in $InnoPaths) {
    if (Get-Command $path -ErrorAction SilentlyContinue) {
        $Iscc = $path
        break
    } elseif (Test-Path $path) {
        $Iscc = $path
        break
    }
}

if ($Iscc) {
    Write-Host "Found Inno Setup at: $Iscc" -ForegroundColor Green
    & $Iscc "installer/audiomasteringtool_setup.iss"
    if ($LASTEXITCODE -eq 0) {
        Write-Host "Windows Installer generated successfully in $DistDir/" -ForegroundColor Green
    }
} else {
    Write-Host "Inno Setup compiler (iscc.exe) not found on PATH. Portable release staged in $StageDir." -ForegroundColor Yellow
}

Write-Host "`n==================================================" -ForegroundColor Cyan
Write-Host " AudioMasteringTool Release Packaging Complete!" -ForegroundColor Cyan
Write-Host "==================================================" -ForegroundColor Cyan
