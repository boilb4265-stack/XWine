<#
.SYNOPSIS
    Prepares an external USB drive for Xbox Series S Developer Mode UWP access.
.DESCRIPTION
    Xbox UWP ignores broadFileSystemAccess. External drives must be formatted NTFS
    and grant recursive Full Control ACLs to ALL APPLICATION PACKAGES (SID S-1-15-2-1).
.EXAMPLE
    .\scripts\prep_xbox_usb.ps1 -DriveLetter E
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidatePattern("^[D-Zd-z]$")]
    [string]$DriveLetter
)

$ErrorActionPreference = "Stop"
$Drive = "${DriveLetter}:\"

$Vol = Get-Volume -DriveLetter $DriveLetter -ErrorAction Stop
if ($Vol.FileSystem -ne "NTFS") {
    throw "Drive $Drive is formatted as '$($Vol.FileSystem)'. Xbox UWP requires NTFS for ALL APPLICATION PACKAGES ACLs."
}

$XWineDir = Join-Path $Drive "XWine"
New-Item -ItemType Directory -Force -Path $XWineDir | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $XWineDir "games") | Out-Null
New-Item -ItemType Directory -Force -Path (Join-Path $XWineDir "saves") | Out-Null

Write-Host "[XWine] Applying ALL APPLICATION PACKAGES (*S-1-15-2-1) Full Control ACLs to $XWineDir..." -ForegroundColor Cyan
& icacls $XWineDir /grant "*S-1-15-2-1:(OI)(CI)F" /T /C

Write-Host "[XWine] External NTFS USB folder '$XWineDir' is ready for Xbox Series S Dev Mode." -ForegroundColor Green
