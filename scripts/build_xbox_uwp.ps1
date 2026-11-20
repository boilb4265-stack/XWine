<#
.SYNOPSIS
    Builds, tests, packs, and signs Project XWine for Xbox Series S Developer Mode (x64 MSIX).
.DESCRIPTION
    Must be executed on a Windows 10/11 x64 host (or GitHub Actions windows-latest runner)
    with Visual Studio 2022 (Desktop C++ + UWP C++ workload), Windows SDK 10.0.22621+,
    CMake, Ninja, and vcpkg.
.EXAMPLE
    .\scripts\build_xbox_uwp.ps1 -VcpkgRoot C:\vcpkg -Configuration Release
#>
[CmdletBinding()]
param(
    [string]$VcpkgRoot = $env:VCPKG_ROOT,
    [ValidateSet("Release", "RelWithDebInfo", "Debug")]
    [string]$Configuration = "Release",
    [string]$OutputDir = "dist\xbox-uwp",
    [switch]$TargetUwpStore
)

$ErrorActionPreference = "Stop"

function Invoke-Checked {
    param([string]$Label, [scriptblock]$Cmd)
    Write-Host "[XWine] $Label" -ForegroundColor Cyan
    & $Cmd
    if ($LASTEXITCODE -ne 0) {
        throw "Step failed ($Label) with exit code $LASTEXITCODE"
    }
}

$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $RepoRoot "build-xbox-x64"
$DistDir  = Join-Path $RepoRoot $OutputDir
$StageDir = Join-Path $DistDir "staging"

Write-Host "[XWine] Building Xbox Series S x64 target (Config=$Configuration)" -ForegroundColor Cyan

if (-not $VcpkgRoot -or -not (Test-Path (Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"))) {
    throw "VCPKG_ROOT is not set or invalid. Install vcpkg and pass -VcpkgRoot <path>."
}

$ToolchainFile = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
$Triplet = if ($TargetUwpStore) { "x64-uwp" } else { "x64-windows" }

Invoke-Checked "Installing vcpkg dependencies (sdl3, freetype, libpng) for $Triplet" {
    & (Join-Path $VcpkgRoot "vcpkg.exe") install "sdl3:$Triplet" "freetype:$Triplet" "libpng:$Triplet"
}

$CmakeArgs = @(
    "-S", $RepoRoot,
    "-B", $BuildDir,
    "-G", "Ninja",
    "-DCMAKE_BUILD_TYPE=$Configuration",
    "-DCMAKE_TOOLCHAIN_FILE=$ToolchainFile",
    "-DVCPKG_TARGET_TRIPLET=$Triplet",
    "-DXWINE_TARGET_XBOX_UWP=ON"
)
if ($TargetUwpStore) {
    $CmakeArgs += @("-DCMAKE_SYSTEM_NAME=WindowsStore", "-DCMAKE_SYSTEM_VERSION=10.0")
}

Invoke-Checked "Configuring CMake + Ninja (MSVC x64)" { cmake @CmakeArgs }
Invoke-Checked "Compiling XWine + Phase 7 Windows x64 Probe" { cmake --build $BuildDir --parallel }

$VcpkgBinDir = Join-Path $VcpkgRoot "installed\$Triplet\bin"
if (Test-Path $VcpkgBinDir) {
    $env:PATH = "$VcpkgBinDir;$env:PATH"
}

Invoke-Checked "Running automated CTest suite on Windows x64" {
    ctest --test-dir $BuildDir --output-on-failure -V
}

# Verify Phase 7 Probe with xwine-diag map (live Windows x64 relocation, IAT shim & W^X test!)
$ProbeExe = Join-Path $BuildDir "tools\phase7-probe\xwine-phase7-probe.exe"
$DiagExe  = Join-Path $BuildDir "tools\diagnostics\xwine-diag.exe"
if ((Test-Path $ProbeExe) -and (Test-Path $DiagExe)) {
    Invoke-Checked "Inspecting & executing Phase 7 PE32+ probe via xwine-diag" {
        & $DiagExe inspect $ProbeExe
        & $DiagExe map $ProbeExe
    }
}

# Stage MSIX package layout
New-Item -ItemType Directory -Force -Path $DistDir | Out-Null
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $StageDir "Assets") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $StageDir "Probes") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $StageDir "Tools") | Out-Null

Copy-Item (Join-Path $RepoRoot "tools\packaging\Package.appxmanifest") (Join-Path $StageDir "AppxManifest.xml")
Copy-Item (Join-Path $RepoRoot "tools\packaging\Assets\*.png") (Join-Path $StageDir "Assets\")
Copy-Item (Join-Path $BuildDir "launcher\xwine-launcher.exe") (Join-Path $StageDir "XWineLauncher.exe")

# Bundle runtime DLLs produced beside launcher or in vcpkg bin
Get-ChildItem (Join-Path $BuildDir "launcher\*.dll") -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item $_.FullName $StageDir -Force
}
if (Test-Path $VcpkgBinDir) {
    Get-ChildItem (Join-Path $VcpkgBinDir "*.dll") -ErrorAction SilentlyContinue | ForEach-Object {
        Copy-Item $_.FullName $StageDir -Force
    }
}
if (Test-Path $ProbeExe) {
    Copy-Item $ProbeExe (Join-Path $StageDir "Probes\")
}
if (Test-Path $DiagExe) {
    Copy-Item $DiagExe (Join-Path $StageDir "Tools\")
}
$GfxExe = Join-Path $BuildDir "tools\graphics-bench\xwine-gfx-test.exe"
if (Test-Path $GfxExe) {
    Copy-Item $GfxExe (Join-Path $StageDir "Tools\")
}

# Locate Windows SDK MakeAppx.exe and SignTool.exe
$SdkBin = Get-ChildItem "${env:ProgramFiles(x86)}\Windows Kits\10\bin\10.0.*\x64" -Directory -ErrorAction SilentlyContinue |
    Sort-Object Name -Descending | Select-Object -First 1
if (-not $SdkBin) {
    throw "Windows 10/11 SDK x64 tools (makeappx.exe / signtool.exe) not found."
}
$MakeAppx = Join-Path $SdkBin.FullName "makeappx.exe"
$SignTool = Join-Path $SdkBin.FullName "signtool.exe"

$MsixPath = Join-Path $DistDir "XWine-XboxSeriesS-x64-DevMode.msix"
$CerPath  = Join-Path $DistDir "XWine-DevMode-SelfSigned.cer"
$PfxPath  = Join-Path $DistDir "XWine-DevMode-SelfSigned.pfx"

if (Test-Path $MsixPath) { Remove-Item -Force $MsixPath }
Invoke-Checked "Packing $MsixPath with makeappx.exe" {
    & $MakeAppx pack /d $StageDir /p $MsixPath /o
}

Write-Host "[XWine] Generating self-signed Xbox Dev Mode certificate..." -ForegroundColor Cyan
$CertPassword = ConvertTo-SecureString -String "XWineDevModeCert!" -Force -AsPlainText
$Cert = New-SelfSignedCertificate `
    -Type Custom `
    -Subject "CN=ProjectXWineDev" `
    -KeyAlgorithm RSA `
    -KeyLength 2048 `
    -KeyUsage DigitalSignature `
    -FriendlyName "Project XWine Dev Mode Sideloading Cert" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}ca=false")

Export-PfxCertificate -Cert $Cert -FilePath $PfxPath -Password $CertPassword | Out-Null
Export-Certificate -Cert $Cert -FilePath $CerPath | Out-Null

Invoke-Checked "Signing $MsixPath with signtool.exe (SHA256)" {
    & $SignTool sign /fd SHA256 /f $PfxPath /p "XWineDevModeCert!" $MsixPath
}

Write-Host ""
Write-Host "=====================================================================" -ForegroundColor Green
Write-Host " Xbox Dev Mode Package Ready: $MsixPath" -ForegroundColor Green
Write-Host " Dev Mode Certificate       : $CerPath" -ForegroundColor Green
Write-Host " Phase 7 Test Probe         : $ProbeExe" -ForegroundColor Green
Write-Host "=====================================================================" -ForegroundColor Green
