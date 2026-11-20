<#
.SYNOPSIS
    Uploads a signed Project XWine .msix package and certificate to an Xbox Series S
    running Developer Mode via the official Xbox Device Portal REST API.
.EXAMPLE
    .\scripts\deploy_xbox_portal.ps1 -XboxIp 192.168.1.42 -Username dev -Password secret
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$XboxIp,
    [int]$Port = 11443,
    [string]$PackagePath = "dist\xbox-uwp\XWine-XboxSeriesS-x64-DevMode.msix",
    [string]$CertPath = "dist\xbox-uwp\XWine-DevMode-SelfSigned.cer",
    [string]$Username = "",
    [string]$Password = ""
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $PackagePath)) {
    throw "MSIX package not found at '$PackagePath'. Run .\scripts\build_xbox_uwp.ps1 first."
}
if (-not (Test-Path $CertPath)) {
    throw "Certificate file not found at '$CertPath'."
}

$PkgFile = (Resolve-Path $PackagePath).Path
$CerFile = (Resolve-Path $CertPath).Path
$FileName = Split-Path $PkgFile -Leaf
$BaseUrl = "https://${XboxIp}:${Port}"
$InstallUrl = "${BaseUrl}/api/app/packagemanager/package?package=$([Uri]::EscapeDataString($FileName))"

Write-Host "[XWine] Uploading $FileName + certificate to Xbox Device Portal ($BaseUrl)..." -ForegroundColor Cyan

# Use curl.exe (built into Windows 10/11 and macOS) for reliable multipart upload over self-signed Portal TLS
$CurlArgs = @(
    "-k", "-f", "-sS",
    "-X", "POST",
    "-F", "package=@$PkgFile",
    "-F", "cert=@$CerFile"
)
if ($Username) {
    $CurlArgs += @("-u", "${Username}:${Password}")
}
$CurlArgs += $InstallUrl

& curl.exe @CurlArgs

Write-Host ""
Write-Host "[XWine] Upload submitted to Xbox Device Portal." -ForegroundColor Green
Write-Host "MANDATORY STEP ON XBOX SERIES S:" -ForegroundColor Yellow
Write-Host "  1. Open $BaseUrl in your browser." -ForegroundColor Yellow
Write-Host "  2. Under 'My games & apps', locate 'Project XWine (Experimental)'." -ForegroundColor Yellow
Write-Host "  3. Open 'View details' -> set 'App type' from 'App' to 'Game' (~5,120 MiB RAM + full D3D12)." -ForegroundColor Yellow
Write-Host "  4. Reboot console or relaunch XWine and verify 5.0 GB ceiling in Diagnostics." -ForegroundColor Yellow
