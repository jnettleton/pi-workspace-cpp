# debug.ps1 - Launch a remote gdb session: gdbserver on the Pi, gdb-multiarch
# inside the container connecting outward.
#
# Usage:
#   .\scripts\debug.ps1
#   .\scripts\debug.ps1 -PiHost pi@192.168.1.42 -Port 2345

[CmdletBinding()]
param(
    [string]$PiHost   = "jnettleton@raspberrypi",
    [string]$RemoteDir = "~",
    [int]   $Port     = 2345,
    [ValidateSet("Debug", "Release")]
    [string]$Config   = "Debug"
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Image       = "rpi-xc"
$Preset      = if ($Config -eq "Debug") { "pi4-debug" } else { "pi4-release" }
$Binary      = "build/$Preset/bin/rpi_blink"
$LocalBinary = Join-Path $ProjectRoot $Binary.Replace('/', '\')

if (-not (Test-Path $LocalBinary)) {
    Write-Error "Binary not found: $LocalBinary`nRun .\scripts\build.ps1 -Config $Config first."
    exit 1
}

# Resolve the Pi's IP via Windows' resolver (handles mDNS/NetBIOS).
# Containers can't see Bonjour/NetBIOS, but they can route to plain LAN IPs.
$PiHostName = ($PiHost -split "@")[-1]
try {
    $PiAddress = (
        [System.Net.Dns]::GetHostAddresses($PiHostName) |
            Where-Object AddressFamily -eq InterNetwork |
            Select-Object -First 1
    ).IPAddressToString
} catch {
    Write-Error "Could not resolve '$PiHostName' from Windows. Pass -PiHost user@<ip-address>."
    exit 1
}
if (-not $PiAddress) {
    Write-Error "No IPv4 address found for '$PiHostName'."
    exit 1
}
Write-Host "[debug.ps1] Resolved $PiHostName -> $PiAddress" -ForegroundColor DarkGray

# Start gdbserver on the Pi in the background, listening on $Port.
Write-Host "[debug.ps1] Deploying binary..." -ForegroundColor Cyan
scp $LocalBinary "${PiHost}:${RemoteDir}/"
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[debug.ps1] Starting gdbserver on ${PiHost}:${Port}" -ForegroundColor Cyan
$gdbserverCmd = "gdbserver :$Port $RemoteDir/rpi_blink"
# nohup so it survives if our SSH session ends; '&' to background.
Start-Process -NoNewWindow ssh -ArgumentList @($PiHost, "nohup $gdbserverCmd > /tmp/gdbserver.log 2>&1")
Start-Sleep -Seconds 1

# Launch gdb-multiarch from the container, connecting to the Pi.
Write-Host "[debug.ps1] Connecting gdb-multiarch -> ${PiAddress}:${Port}" -ForegroundColor Cyan
docker run --rm -it -v "${ProjectRoot}:/src" $Image gdb-multiarch `
    -ex "set sysroot target:/" `
    -ex "target remote ${PiAddress}:${Port}" `
    "/src/$Binary"
