[CmdletBinding()]
param(
    [string]$ProbePath = "windows\build\Release\droidcam_clone_vcam_probe.exe",
    [string]$OutputDirectory = "windows\out\test-vcam",
    [int]$DurationSeconds = 30
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $ProbePath)) {
    Write-Error "Probe executable not found at: $ProbePath"
    exit 1
}

$probeFull = (Resolve-Path $ProbePath).Path
$probeDir = Split-Path -Parent $probeFull
$dllPath = Join-Path $probeDir "droidcam_clone_virtual_camera.dll"

if (-not (Test-Path $dllPath)) {
    Write-Warning "Virtual camera DLL not found at expected location: $dllPath"
}

if (-not (Test-Path $OutputDirectory)) {
    New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
}

$logFile = Join-Path $OutputDirectory "test-virtual-camera.log"
"=== Virtual Camera Diagnostic Test - $(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') ===" | Out-File $logFile

# System Information
$os = Get-CimInstance Win32_OperatingSystem
"OS: $($os.Caption) ($($os.Version), Build $($os.BuildNumber))" | Tee-Object -FilePath $logFile -Append

# Registration Check
Write-Host "Running registration probe..."
$probeProc = Start-Process -FilePath $probeFull -ArgumentList "--register" -NoNewWindow -PassThru -Wait
$regExitCode = $probeProc.ExitCode
"Register Exit Code: $regExitCode" | Tee-Object -FilePath $logFile -Append

if ($regExitCode -ne 0) {
    $elevationMsg = "Virtual camera COM registration requires Administrator privileges to register the Media Source in HKLM so that the Windows Camera Frame Server (LOCAL SERVICE) can load it.`nTo register manually as Administrator:`n  regsvr32 `"$dllPath`"`nor:`n  Start-Process `"$probeFull`" -ArgumentList '--register' -Verb RunAs"
    Write-Warning $elevationMsg
    $elevationMsg | Out-File -FilePath $logFile -Append
}


# Check Media Foundation Device Enumeration
Write-Host "Checking Media Foundation virtual camera enumeration..."
Add-Type -AssemblyName System.Drawing
$cameras = Get-CimInstance Win32_PnPEntity | Where-Object { $_.PNPClass -eq "Camera" -or $_.Caption -like "*DroidCam*" }
if ($cameras) {
    "Detected Cameras in Device Manager:" | Tee-Object -FilePath $logFile -Append
    $cameras | ForEach-Object { " - $($_.Caption) (Status: $($_.Status), DeviceID: $($_.DeviceID))" } | Tee-Object -FilePath $logFile -Append
} else {
    "No cameras matching DroidCam detected in PnPEntity." | Tee-Object -FilePath $logFile -Append
}

# Consumer Detection
$obsProc = Get-Process -Name "obs64", "obs32" -ErrorAction SilentlyContinue
$discordProc = Get-Process -Name "Discord" -ErrorAction SilentlyContinue

"OBS Running: $([bool]$obsProc)" | Tee-Object -FilePath $logFile -Append
"Discord Running: $([bool]$discordProc)" | Tee-Object -FilePath $logFile -Append

# Run bounded synthetic producer if registered successfully
if ($regExitCode -eq 0) {
    Write-Host "Running synthetic producer for $DurationSeconds seconds..."
    $synthOutput = & $probeFull --synthetic --duration $DurationSeconds 2>&1
    "Synthetic run output:" | Tee-Object -FilePath $logFile -Append
    $synthOutput | Tee-Object -FilePath $logFile -Append
} else {
    Write-Warning "Registration failed with exit code $regExitCode. Skipping synthetic streaming test."
}

Write-Host "Diagnostic test finished. Report saved to: $logFile"
