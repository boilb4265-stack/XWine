<#
.SYNOPSIS
    Builds and signs Project XWine for Xbox Series S Developer Mode (x64 MSIX/APPX).
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
$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path
$BuildDir = Join-Path $RepoRoot "build-xbox-x64"
$StageDir = Join-Path $RepoRoot "$OutputDir\staging"
$DistDir  = Join-Path $RepoRoot $OutputDir

Write-Host "[XWine] Building Xbox Series S x64 target (Config=$Configuration)" -ForegroundColor Cyan

if (-not $VcpkgRoot -or -not (Test-Path (Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"))) {
    throw "VCPKG_ROOT is not set or invalid. Install vcpkg and pass -VcpkgRoot <path>."
}

$ToolchainFile = Join-Path $VcpkgRoot "scripts\buildsystems\vcpkg.cmake"
$Triplet = if ($TargetUwpStore) { "x64-uwp" } else { "x64-windows" }

Write-Host "[XWine] Ensuring vcpkg dependencies (sdl3, freetype, libpng) for triplet $Triplet..." -ForegroundColor Cyan
& (Join-Path $VcpkgRoot "vcpkg.exe") install "sdl3:$Triplet" "freetype:$Triplet" "libpng:$Triplet"

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

Write-Host "[XWine] Configuring CMake..." -ForegroundColor Cyan
cmake @CmakeArgs
Write-Host "[XWine] Compiling XWine + Phase 7 Windows Probe..." -ForegroundColor Cyan
cmake --build $BuildDir --parallel

Write-Host "[XWine] Running automated CTest suite on Windows host..." -ForegroundColor Cyan
ctest --test-dir $BuildDir --output-on-failure

# Stage MSIX package layout
if (Test-Path $StageDir) { Remove-Item -Recurse -Force $StageDir }
New-Item -ItemType Directory -Force -Path $StageDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $StageDir "Assets") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $StageDir "Probes") | Out-Null

Copy-Item (Join-Path $RepoRoot "tools\packaging\Package.appxmanifest") (Join-Path $StageDir "AppxManifest.xml")
Copy-Item (Join-Path $RepoRoot "tools\packaging\Assets\*.png") (Join-Path $StageDir "Assets\")
Copy-Item (Join-Path $BuildDir "launcher\xwine-launcher.exe") (Join-Path $StageDir "XWineLauncher.exe")

# Bundle runtime DLLs produced beside launcher
Get-ChildItem (Join-Path $BuildDir "launcher\*.dll") -ErrorAction SilentlyContinue | ForEach-Object {
    Copy-Item $_.FullName $StageDir
}
if (Test-Path (Join-Path $BuildDir "tools\phase7-probe\xwine-phase7-probe.exe")) {
    Copy-Item (Join-Path $BuildDir "tools\phase7-probe\xwine-phase7-probe.exe") (Join-Path $StageDir "Probes\")
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
Write-Host "[XWine] Packing $MsixPath..." -ForegroundColor Cyan
& $MakeAppx pack /d $StageDir /p $MsixPath /o

# Create self-signed Dev Mode test certificate (CN=ProjectXWineDev matches AppxManifest Publisher)
Write-Host "[XWine] Generating & applying self-signed Xbox Dev Mode certificate..." -ForegroundColor Cyan
$CertPassword = ConvertTo-SecureString -String "XWineDevModeCert!" -Force -AsPlainText
$Cert = New-SelfSignedCertificate `
    -Type Custom `
    -Subject "CN=ProjectXWineDev" `
    -KeyUsage DigitalSignature `
    -FriendlyName "Project XWine Dev Mode Sideloading Cert" `
    -CertStoreLocation "Cert:\CurrentUser\My" `
    -TextExtension @("2.5.29.37={text}1.3.6.1.5.5.7.3.3", "2.5.29.19={text}")

Export-PfxCertificate -Cert $Cert -FilePath $PfxPath -Password $CertPassword | Out-Null
Export-Certificate -Cert $Cert -FilePath $CerPath | Out-Null

& $SignTool sign /fd SHA256 /a /f $PfxPath /p "XWineDevModeCert!" $MsixPath

Write-Host ""
Write-Host "=====================================================================" -ForegroundColor Green
Write-Host " Xbox Dev Mode Package Ready: $MsixPath" -ForegroundColor Green
Write-Host " Dev Mode Certificate       : $CerPath" -ForegroundColor Green
Write-Host " Next: Run .\scripts\deploy_xbox_portal.ps1 -XboxIp <CONSOLE_IP>" -ForegroundColor Yellow
Write-Host "       Then switch XWine from 'App' to 'Game' in Xbox Device Portal." -ForegroundColor Yellow
Write-Host "=====================================================================" -ForegroundColor Green
