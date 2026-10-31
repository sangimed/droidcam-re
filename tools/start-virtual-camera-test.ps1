<#
.SYNOPSIS
Starts the Windows receiver and Android sender for a local virtual-camera test.

.DESCRIPTION
Checks that DroidCam Video is registered, starts or reuses the Windows receiver,
and launches the Android app on one authorized ADB device. Camera registration is
one-time; use -RegisterCamera only when the device is missing. Use -Build to build
the Windows Release client and Android debug APK before launching.

.EXAMPLE
.\start-virtual-camera-test.ps1

.EXAMPLE
.\start-virtual-camera-test.ps1 -Build

.EXAMPLE
.\start-virtual-camera-test.ps1 -DeviceSerial d3ec6d70 -RegisterCamera
#>
[CmdletBinding()]
param(
    [string]$DeviceSerial,
    [switch]$Build,
    [switch]$RegisterCamera
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$androidPackage = 'dev.droidcamclone.app'
$receiverPath = Join-Path $repoRoot 'windows\build\Release\droidcam_clone.exe'
$probePath = Join-Path $repoRoot 'windows\build\Release\droidcam_clone_vcam_probe.exe'
$androidApk = Join-Path $repoRoot 'android\app\build\outputs\apk\debug\app-debug.apk'

function Invoke-CheckedCommand {
    param(
        [Parameter(Mandatory)][string]$FilePath,
        [Parameter(Mandatory)][string[]]$Arguments,
        [Parameter(Mandatory)][string]$Description
    )

    & $FilePath @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Description failed with exit code $LASTEXITCODE."
    }
}

$adb = Get-Command adb.exe -ErrorAction Stop
$adbRows = @(& $adb.Source devices)
if ($LASTEXITCODE -ne 0) { throw 'Could not query Android devices through adb.' }
$connectedSerials = @(
    $adbRows | Select-Object -Skip 1 | ForEach-Object {
        if ($_ -match '^([^\s]+)\s+device$') { $Matches[1] }
    }
)

if ([string]::IsNullOrWhiteSpace($DeviceSerial)) {
    if ($connectedSerials.Count -ne 1) {
        throw "Expected exactly one authorized Android device; found $($connectedSerials.Count). Pass -DeviceSerial to choose one."
    }
    $DeviceSerial = $connectedSerials[0]
} elseif ($DeviceSerial -notin $connectedSerials) {
    throw "Android device '$DeviceSerial' is not connected and authorized. Run 'adb devices -l' to inspect the connection."
}

Write-Host "Android device: $DeviceSerial"

if ($Build) {
    Invoke-CheckedCommand -FilePath 'cmake' -Arguments @('--build', (Join-Path $repoRoot 'windows\build'), '--config', 'Release', '--parallel') -Description 'Windows Release build'
    Push-Location (Join-Path $repoRoot 'android')
    try {
        Invoke-CheckedCommand -FilePath '.\gradlew.bat' -Arguments @('assembleDebug') -Description 'Android debug build'
    } finally {
        Pop-Location
    }
}

if (-not (Test-Path -LiteralPath $receiverPath)) {
    throw "Windows receiver not found at '$receiverPath'. Run this script with -Build."
}

$camera = Get-PnpDevice -Class MEDIA -ErrorAction SilentlyContinue |
    Where-Object { $_.FriendlyName -eq 'DroidCam Video' -and $_.Status -eq 'OK' } |
    Select-Object -First 1

if (-not $camera -and $RegisterCamera) {
    if (-not (Test-Path -LiteralPath $probePath)) {
        throw "Virtual camera probe not found at '$probePath'. Run this script with -Build first."
    }
    Write-Host 'Requesting administrator approval to register the Windows virtual camera...'
    $registerProcess = Start-Process -FilePath $probePath -ArgumentList '--register' -Verb RunAs -Wait -PassThru
    if ($registerProcess.ExitCode -ne 0) {
        throw "Virtual camera registration failed with exit code $($registerProcess.ExitCode)."
    }
    $camera = Get-PnpDevice -Class MEDIA -ErrorAction SilentlyContinue |
        Where-Object { $_.FriendlyName -eq 'DroidCam Video' -and $_.Status -eq 'OK' } |
        Select-Object -First 1
}

if ($camera) {
    Write-Host "Windows virtual camera: $($camera.FriendlyName) [$($camera.InstanceId)] - $($camera.Status)"
} else {
    Write-Warning 'DroidCam Video is not registered or is not ready. Re-run with -RegisterCamera to request one-time administrator registration.'
}

$receiver = Get-Process -Name 'droidcam_clone' -ErrorAction SilentlyContinue | Select-Object -First 1
if ($receiver) {
    Write-Host "Windows receiver already running (PID $($receiver.Id))."
} else {
    $listener = Get-NetTCPConnection -State Listen -LocalPort 48200 -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if ($listener) {
        throw "TCP port 48200 is already occupied by process $($listener.OwningProcess); not starting another receiver."
    }
    $receiver = Start-Process -FilePath $receiverPath -WorkingDirectory (Split-Path -Parent $receiverPath) -PassThru
    Start-Sleep -Milliseconds 700
    if ($receiver.HasExited) { throw "Windows receiver exited during startup (code $($receiver.ExitCode))." }
    Write-Host "Started Windows receiver (PID $($receiver.Id)); waiting on TCP port 48200."
}

$installedPackage = @(& $adb.Source -s $DeviceSerial shell pm path $androidPackage 2>$null)
$isInstalled = ($LASTEXITCODE -eq 0 -and $installedPackage.Count -gt 0)

if ($Build -or -not $isInstalled) {
    if (-not (Test-Path -LiteralPath $androidApk)) {
        throw "Android debug APK not found at '$androidApk'. Run this script with -Build."
    }
    Invoke-CheckedCommand -FilePath $adb.Source -Arguments @('-s', $DeviceSerial, 'install', '-r', $androidApk) -Description 'Android app installation'
}

Invoke-CheckedCommand -FilePath $adb.Source -Arguments @('-s', $DeviceSerial, 'shell', 'am', 'start', '-n', "$androidPackage/.MainActivity") -Description 'Android app launch'
Write-Host 'Android app launched. Enter one of the Windows IPv4 addresses below and tap Connect when ready.'

$physicalAliases = @(
    Get-NetAdapter -Physical -ErrorAction SilentlyContinue |
        Where-Object Status -eq 'Up' |
        ForEach-Object Name
)
$localAddresses = @(
    Get-NetIPAddress -AddressFamily IPv4 -ErrorAction SilentlyContinue |
        Where-Object {
            $_.AddressState -eq 'Preferred' -and
            $_.IPAddress -notmatch '^(127|169\.254)\.' -and
            ($physicalAliases.Count -eq 0 -or $_.InterfaceAlias -in $physicalAliases)
        } |
        Sort-Object InterfaceAlias, IPAddress |
        ForEach-Object { "$($_.IPAddress)  ($($_.InterfaceAlias))" }
)
if ($localAddresses.Count -gt 0) {
    Write-Host 'Windows IPv4 addresses on active physical adapters (choose the one reachable from Android):'
    $localAddresses | ForEach-Object { Write-Host "  $_" }
} else {
    Write-Warning 'No usable Windows IPv4 address was found.'
}
