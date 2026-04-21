# build.ps1 - Cross-compile rpi-blink for Raspberry Pi 4 inside Docker.
#
# Usage:
#   .\scripts\build.ps1                 # debug build
#   .\scripts\build.ps1 -Config Release # release build
#   .\scripts\build.ps1 -Rebuild        # nuke build dir, full reconfigure

[CmdletBinding()]
param(
    [ValidateSet("Debug", "Release")]
    [string]$Config = "Debug",
    [switch]$Rebuild
)

$ErrorActionPreference = "Stop"
$ProjectRoot = Split-Path -Parent $PSScriptRoot
$Image       = "rpi-xc"
$Preset      = if ($Config -eq "Debug") { "pi4-debug" } else { "pi4-release" }

# 1. Make sure Docker is reachable.
try { docker info *>$null } catch {
    Write-Error "Docker is not running. Start Docker Desktop and try again."
    exit 1
}

# 2. Build the image if it doesn't exist or the Dockerfile is newer.
#    'docker images -q' is silent for missing images (no stderr noise).
$Dockerfile = Join-Path $ProjectRoot "docker\Dockerfile"
$rebuildImage = $true
$imageId = (docker images -q $Image 2>$null | Out-String).Trim()
if ($imageId) {
    $created = (docker inspect --format "{{.Created}}" $Image 2>$null | Out-String).Trim()
    if ($created) {
        try {
            $imageDate  = [datetime]::Parse($created)
            $dockerDate = (Get-Item $Dockerfile).LastWriteTime
            if ($imageDate -gt $dockerDate) { $rebuildImage = $false }
        } catch {
            # Fall through and rebuild on any parse problem.
        }
    }
}
if ($rebuildImage) {
    Write-Host "[build.ps1] Building Docker image '$Image'..." -ForegroundColor Cyan
    docker build -t $Image -f $Dockerfile $ProjectRoot
    if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}

# 3. Optional clean.
if ($Rebuild) {
    $buildDir = Join-Path $ProjectRoot "build\$Preset"
    if (Test-Path $buildDir) {
        Write-Host "[build.ps1] Removing $buildDir" -ForegroundColor Yellow
        Remove-Item -Recurse -Force $buildDir
    }
}

# 4. Configure + build inside the container.
#    Mount the project at /src; CMakePresets.json's binaryDir lands at build/<preset>.
Write-Host "[build.ps1] Configuring (preset: $Preset)..." -ForegroundColor Cyan
docker run --rm -v "${ProjectRoot}:/src" $Image `
    cmake --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "[build.ps1] Building..." -ForegroundColor Cyan
docker run --rm -v "${ProjectRoot}:/src" $Image `
    cmake --build --preset $Preset
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

$binary = Join-Path $ProjectRoot "build\$Preset\bin\rpi_blink"
Write-Host "[build.ps1] Done. Binary: $binary" -ForegroundColor Green