<#
.SYNOPSIS
    Builds and packages the AudioMasteringTool Windows 1.0 release.
.DESCRIPTION
    Configures and builds the production MSVC/ONNX path, runs CTest, stages a
    self-contained portable tree, verifies required runtime DLLs, optionally
    signs binaries, writes provenance and SHA-256 checksums, and compiles the
    Inno Setup installer when available.
#>

param(
    [string]$BuildDir = "build/windows-msvc",
    [string]$DistDir = "dist",
    [switch]$SkipTests,
    [switch]$SkipInstaller,
    [switch]$AllowDirty,
    [switch]$RequireSigning,
    [string]$SignToolPath = "",
    [string]$SigningCertificateThumbprint = "",
    [string]$Mode1ApprovalEvidence = ""
)

$ErrorActionPreference = "Stop"
Set-StrictMode -Version Latest

$AppVersion = "1.0.0"
$LibSndFileVersion = "1.2.2"
$LibSndFileSha256 = "2173935c0c1ed13cf627951d34483f9d405ead2eb473190461c42ba220643a3f"
$LibSndFileUrl = "https://github.com/libsndfile/libsndfile/releases/download/$LibSndFileVersion/libsndfile-$LibSndFileVersion-win64.zip"
$LibSndFileLicenseUrl = "https://raw.githubusercontent.com/libsndfile/libsndfile/$LibSndFileVersion/COPYING"
$LibSndFileLicenseSha256 = "ad01ea5cd2755f6048383c8d54c88459cd6fcb17757c5c8892f8c5ea060f6140"

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
Set-Location $RepoRoot
$BuildPath = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $BuildDir))
$DistPath = [System.IO.Path]::GetFullPath((Join-Path $RepoRoot $DistDir))
$StageDir = Join-Path $DistPath "AudioMasteringTool-$AppVersion-win64"

function Invoke-Checked {
    param([string]$Label, [scriptblock]$Command)
    Write-Host $Label -ForegroundColor Yellow
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$Label failed with exit code $LASTEXITCODE."
    }
}

function Find-Tool {
    param([string[]]$Candidates)
    foreach ($candidate in $Candidates) {
        $command = Get-Command $candidate -ErrorAction SilentlyContinue
        if ($command) { return $command.Source }
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

function Sign-ReleaseFile {
    param([string]$Path)
    if (-not $SignToolPath -or -not $SigningCertificateThumbprint) {
        if ($RequireSigning) {
            throw "Release signing is required, but SignToolPath or SigningCertificateThumbprint is missing."
        }
        return
    }
    & $SignToolPath sign /sha1 $SigningCertificateThumbprint /fd SHA256 /td SHA256 `
        /tr "http://timestamp.digicert.com" $Path
    if ($LASTEXITCODE -ne 0) { throw "Signing failed for $Path." }
}

Write-Host "AudioMasteringTool $AppVersion Windows Release Packager" -ForegroundColor Cyan

$GitCommit = (git.exe rev-parse HEAD).Trim()
if ($LASTEXITCODE -ne 0 -or -not $GitCommit) { throw "Unable to resolve the source commit." }
$DirtyEntries = @(git.exe status --porcelain)
if ($LASTEXITCODE -ne 0) { throw "Unable to inspect the source worktree." }
$SourceDirty = $DirtyEntries.Count -gt 0
if ($SourceDirty -and -not $AllowDirty) {
    throw "Refusing to create release artifacts from a dirty worktree. Commit the release changes or pass -AllowDirty for a non-release smoke package."
}

if ($BuildDir -eq "build/windows-msvc") {
    Invoke-Checked "[1/6] Configuring the pinned Windows production preset..." {
        cmake.exe --preset windows-msvc
    }
} else {
    Invoke-Checked "[1/6] Configuring the custom Windows build directory..." {
        cmake.exe -S $RepoRoot -B $BuildPath -A x64 `
            -DAMT_BUILD_TESTS=ON -DAMT_BUILD_BENCHMARKS=ON `
            -DAMT_WITH_ONNX=ON -DAMT_AUTO_FETCH_ONNX_RUNTIME=ON `
            -DAMT_ORT_ENABLE_CUDA=OFF
    }
}

Invoke-Checked "[2/6] Building MSVC Release..." {
    cmake.exe --build $BuildPath --config Release
}

if (-not $SkipTests) {
    Invoke-Checked "[3/6] Running the complete Release CTest suite..." {
        ctest.exe --test-dir $BuildPath -C Release --timeout 180 --output-on-failure
    }
} else {
    Write-Host "[3/6] Tests explicitly skipped; output is not release-qualified." -ForegroundColor DarkYellow
}

Write-Host "[4/6] Staging the self-contained portable tree..." -ForegroundColor Yellow
if ((Split-Path -Leaf $StageDir) -ne "AudioMasteringTool-$AppVersion-win64") {
    throw "Refusing to replace an unexpected staging path: $StageDir"
}
if (Test-Path -LiteralPath $StageDir) {
    Remove-Item -LiteralPath $StageDir -Recurse -Force
}
New-Item -ItemType Directory -Path $StageDir -Force | Out-Null

$RequiredBinaries = @(
    (Join-Path $BuildPath "src/app/Release/audiomasteringtool.exe"),
    (Join-Path $BuildPath "src/worker/Release/amt_worker.exe"),
    (Join-Path $BuildPath "src/cli/Release/amt_cli.exe")
)
foreach ($binary in $RequiredBinaries) {
    if (-not (Test-Path -LiteralPath $binary -PathType Leaf)) {
        throw "Required release binary is missing: $binary"
    }
    Copy-Item -LiteralPath $binary -Destination $StageDir
}

$DllRoots = @(
    (Join-Path $BuildPath "src/app/Release"),
    (Join-Path $BuildPath "src/worker/Release"),
    (Join-Path $BuildPath "_deps")
)
foreach ($root in $DllRoots) {
    if (Test-Path -LiteralPath $root -PathType Container) {
        Get-ChildItem -LiteralPath $root -Filter "*.dll" -Recurse -File |
            ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $StageDir -Force }
    }
}

if (-not (Test-Path -LiteralPath (Join-Path $StageDir "sndfile.dll") -PathType Leaf)) {
    $DependencyDir = Join-Path $BuildPath "release-deps/libsndfile-$LibSndFileVersion"
    $ArchivePath = Join-Path $BuildPath "release-deps/libsndfile-$LibSndFileVersion-win64.zip"
    New-Item -ItemType Directory -Path (Split-Path -Parent $ArchivePath) -Force | Out-Null
    if (-not (Test-Path -LiteralPath $ArchivePath -PathType Leaf) -or
        (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant() -ne $LibSndFileSha256) {
        Invoke-WebRequest -Uri $LibSndFileUrl -OutFile $ArchivePath
    }
    $ActualHash = (Get-FileHash -LiteralPath $ArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($ActualHash -ne $LibSndFileSha256) {
        throw "libsndfile archive SHA-256 mismatch: $ActualHash"
    }
    if (Test-Path -LiteralPath $DependencyDir) {
        Remove-Item -LiteralPath $DependencyDir -Recurse -Force
    }
    Expand-Archive -LiteralPath $ArchivePath -DestinationPath $DependencyDir
    $SndFileDll = Get-ChildItem -LiteralPath $DependencyDir -Filter "sndfile.dll" -Recurse -File |
        Select-Object -First 1
    if (-not $SndFileDll) { throw "The verified libsndfile archive contains no sndfile.dll." }
    Copy-Item -LiteralPath $SndFileDll.FullName -Destination $StageDir
}

foreach ($requiredDll in @("sndfile.dll", "onnxruntime.dll")) {
    if (-not (Test-Path -LiteralPath (Join-Path $StageDir $requiredDll) -PathType Leaf)) {
        throw "Required runtime DLL was not staged: $requiredDll"
    }
}

Copy-Item -LiteralPath (Join-Path $RepoRoot "models") -Destination $StageDir -Recurse
Copy-Item -LiteralPath (Join-Path $RepoRoot "docs") -Destination $StageDir -Recurse
$LicenseDir = Join-Path $StageDir "licenses"
New-Item -ItemType Directory -Path $LicenseDir -Force | Out-Null
Get-ChildItem -LiteralPath (Join-Path $RepoRoot "third_party/licenses") -File |
    ForEach-Object { Copy-Item -LiteralPath $_.FullName -Destination $LicenseDir }
$LibSndFileLicensePath = Join-Path $LicenseDir "libsndfile-LGPL-2.1.txt"
Invoke-WebRequest -Uri $LibSndFileLicenseUrl -OutFile $LibSndFileLicensePath
$ActualLicenseHash = (Get-FileHash -LiteralPath $LibSndFileLicensePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($ActualLicenseHash -ne $LibSndFileLicenseSha256) {
    throw "libsndfile licence text SHA-256 mismatch: $ActualLicenseHash"
}
foreach ($document in @(
    "LICENSE", "README.md", "SECURITY.md", "THIRD_PARTY_NOTICES.md",
    "MODEL_LICENSES.md", "third_party/NOTICE.md", "third_party/dependencies.json"
)) {
    $source = Join-Path $RepoRoot $document
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Required release document is missing: $document"
    }
    Copy-Item -LiteralPath $source -Destination $StageDir
}

if (-not $SignToolPath -and $SigningCertificateThumbprint) {
    $SignToolPath = Find-Tool @("signtool.exe")
}
foreach ($binary in Get-ChildItem -LiteralPath $StageDir -Filter "*.exe" -File) {
    Sign-ReleaseFile $binary.FullName
}

$Registry = Get-Content -LiteralPath (Join-Path $StageDir "models/registry.json") -Raw |
    ConvertFrom-Json
$Mode1Approved = [bool]$Registry.models[0].automaticMode1Approved
if ($Mode1Approved) {
    if (-not $Mode1ApprovalEvidence -or
        -not (Test-Path -LiteralPath $Mode1ApprovalEvidence -PathType Leaf)) {
        throw "Mode 1 is approved in the registry, but no calibration approval evidence was supplied."
    }
    $ApprovalReport = Get-Content -LiteralPath $Mode1ApprovalEvidence -Raw |
        ConvertFrom-Json
    if (-not $ApprovalReport.approval -or
        -not [bool]$ApprovalReport.approval.approved) {
        throw "The supplied Mode 1 calibration report does not contain approval.approved=true."
    }
    Copy-Item -LiteralPath $Mode1ApprovalEvidence -Destination `
        (Join-Path $StageDir "mode1-approval-evidence.json")
}
$Provenance = [ordered]@{
    schemaVersion = 1
    appVersion = $AppVersion
    gitCommit = $GitCommit
    sourceDirty = $SourceDirty
    testsSkipped = [bool]$SkipTests
    createdUtc = [DateTime]::UtcNow.ToString("o")
    activeSeparationModel = $Registry.activeSeparationModel
    automaticMode1Approved = $Mode1Approved
}
$Provenance | ConvertTo-Json -Depth 4 |
    Set-Content -LiteralPath (Join-Path $StageDir "release-provenance.json") -Encoding utf8

$ChecksumLines = Get-ChildItem -LiteralPath $StageDir -Recurse -File |
    Where-Object { $_.Name -ne "SHA256SUMS.txt" } |
    Sort-Object FullName |
    ForEach-Object {
        # Windows PowerShell 5.1 uses .NET Framework, which has no
        # System.IO.Path.GetRelativePath. Every item here is already known to
        # be below the staging root, so a guarded prefix removal is sufficient.
        $stagePrefix = $StageDir.TrimEnd([char[]]@('\', '/')) + [System.IO.Path]::DirectorySeparatorChar
        if (-not $_.FullName.StartsWith($stagePrefix, [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to checksum a file outside the staging root: $($_.FullName)"
        }
        $relative = $_.FullName.Substring($stagePrefix.Length).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash  $relative"
    }
$ChecksumLines | Set-Content -LiteralPath (Join-Path $StageDir "SHA256SUMS.txt") -Encoding ascii

$PortableArchivePath = Join-Path $DistPath "AudioMasteringTool-$AppVersion-win64.zip"
Compress-Archive -LiteralPath $StageDir -DestinationPath $PortableArchivePath `
    -CompressionLevel Optimal -Force
$PortableArchiveHash = (Get-FileHash -LiteralPath $PortableArchivePath -Algorithm SHA256).Hash.ToLowerInvariant()
"$PortableArchiveHash  $(Split-Path -Leaf $PortableArchivePath)" |
    Set-Content -LiteralPath (Join-Path $DistPath "AudioMasteringTool-$AppVersion-win64.sha256") -Encoding ascii

Write-Host "[5/6] Portable tree and archive verified: $PortableArchivePath" -ForegroundColor Green

if (-not $SkipInstaller) {
    $Iscc = Find-Tool @(
        "iscc.exe",
        "C:\Program Files (x86)\Inno Setup 6\iscc.exe",
        "C:\Program Files\Inno Setup 6\iscc.exe"
    )
    if (-not $Iscc) { throw "Inno Setup 6 is required unless -SkipInstaller is specified." }
    Invoke-Checked "[6/6] Compiling the installer..." {
        & $Iscc (Join-Path $RepoRoot "installer/audiomasteringtool_setup.iss")
    }
    $InstallerPath = Join-Path $DistPath "AudioMasteringTool_Setup_${AppVersion}_win64.exe"
    if (-not (Test-Path -LiteralPath $InstallerPath -PathType Leaf)) {
        throw "Inno Setup completed without producing the expected installer."
    }
    Sign-ReleaseFile $InstallerPath
    $InstallerHash = (Get-FileHash -LiteralPath $InstallerPath -Algorithm SHA256).Hash.ToLowerInvariant()
    "$InstallerHash  $(Split-Path -Leaf $InstallerPath)" |
        Set-Content -LiteralPath (Join-Path $DistPath "AudioMasteringTool_Setup_${AppVersion}_win64.sha256") -Encoding ascii
} else {
    Write-Host "[6/6] Installer explicitly skipped; portable tree only." -ForegroundColor DarkYellow
}

Write-Host "Packaging completed successfully." -ForegroundColor Cyan
