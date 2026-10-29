param(
    [Parameter(Mandatory=$true)][ValidatePattern('^[A-Za-z0-9_.:-]+$')][string]$DeviceSerial,
    [ValidatePattern('^[A-Za-z0-9_.]+$')][string]$Package = 'dev.droidcamclone.app',
    [Parameter(Mandatory=$true)][int]$ReceiverProcessId,
    [Parameter(Mandatory=$true)][string]$TabletAddress,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateRange(1, 120)][int]$DurationMinutes = 60,
    [ValidateRange(10, 60)][int]$IntervalSeconds = 30
)

$ErrorActionPreference = 'Stop'
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$samplesPath = Join-Path $OutputDirectory 'samples.jsonl'
if (Test-Path -LiteralPath $samplesPath) { throw 'Use a new output directory for each run.' }

function Invoke-Device([string[]]$Arguments) {
    $output = @(& adb -s $DeviceSerial @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) { throw ($output -join [Environment]::NewLine) }
    $output | ForEach-Object { $_.ToString() }
}

function Read-Thermal {
    $lines = @(Invoke-Device @('shell','dumpsys','thermalservice'))
    $statusLine = $lines | Select-String '^Thermal Status:\s*(\d+)' | Select-Object -First 1
    $status = if ($statusLine) { [int]$statusLine.Matches[0].Groups[1].Value } else { $null }
    $source = 'cached'
    $header = $lines | Select-String '^Current temperatures from HAL:' | Select-Object -First 1
    if ($header) {
        $lines = @($lines | Select-Object -Skip $header.LineNumber)
        $end = $lines | Select-String '^Current cooling devices' | Select-Object -First 1
        if ($end) { $lines = @($lines | Select-Object -First ($end.LineNumber - 1)) }
        $source = 'current HAL'
    }
    $readings = @($lines | Select-String 'Temperature\{mValue=([^,]+), mType=([0-3]), mName=([^,]+), mStatus=(\d+)' | ForEach-Object {
        [pscustomobject]@{
            Name = $_.Matches[0].Groups[3].Value
            Type = [int]$_.Matches[0].Groups[2].Value
            Celsius = [double]::Parse($_.Matches[0].Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
            Status = [int]$_.Matches[0].Groups[4].Value
        }
    })
    [pscustomobject]@{ Status=$status; Source=$source; Readings=$readings }
}

function Read-Camera {
    $lines = @(Invoke-Device @('shell','dumpsys','media.camera'))
    $active = $lines | Select-String ('^\(Camera ID: ([^,]+),.*Client Package Name: '+[regex]::Escape($Package)+',') | Select-Object -First 1
    if (-not $active) { throw 'The app no longer owns an active camera.' }
    $cameraId = $active.Matches[0].Groups[1].Value
    $start = ($lines | Select-String ('^== Camera device '+[regex]::Escape($cameraId)+' ') | Select-Object -First 1).LineNumber-1
    $consumer = ''
    $width = 0
    $height = 0
    $streams = @()
    for ($i=$start+1; $i -lt $lines.Count; $i++) {
        $line = $lines[$i]
        if ($line -match '^== ') { break }
        if ($line -match 'Consumer name: (.+)') { $consumer=$Matches[1].Trim() }
        if ($line -match 'Dims: (\d+) x (\d+)') { $width=[int]$Matches[1]; $height=[int]$Matches[2] }
        if ($line -match 'Frames produced: (\d+), last timestamp: (\d+) ns') {
            $streams += [pscustomobject]@{ Consumer=$consumer; Width=$width; Height=$height; Frames=[long]$Matches[1]; TimestampNs=[long]$Matches[2] }
        }
    }
    [pscustomobject]@{ Id=$cameraId; Streams=$streams }
}

$flags = @(Invoke-Device @('shell','dumpsys','package',$Package)) | Select-String '^\s+pkgFlags=' | Select-Object -First 1
if (-not $flags -or $flags.Line -match '\bDEBUGGABLE\b') { throw 'A non-debuggable benchmark or release APK is required.' }
$tabletProcessId = ((Invoke-Device @('shell','pidof',$Package)) -join '').Trim()
if ($tabletProcessId -notmatch '^\d+$') { throw 'Expected a single running app process.' }
$clockTicks = [double]((Invoke-Device @('shell','getconf','CLK_TCK')) -join '').Trim()
$start = [DateTime]::UtcNow
$timer = [Diagnostics.Stopwatch]::StartNew()
$index = 0
$sampleCount = 0
$previousTabletTicks = $null
$previousWindowsCpu = $null
$previousElapsed = $null
$failure = $null
$cancelled = $false

while ($true) {
    if (Test-Path -LiteralPath (Join-Path $OutputDirectory 'stop')) { $cancelled=$true; break }
    $elapsed = $timer.Elapsed.TotalSeconds
    $errors = @()
    $record = [ordered]@{ Time=[DateTime]::UtcNow.ToString('o'); ElapsedSeconds=$elapsed; Index=$index }
    try {
        $currentPid = ((Invoke-Device @('shell','pidof',$Package)) -join '').Trim()
        if ($currentPid -ne $tabletProcessId) { throw 'Android app process changed or exited.' }
        $receiver = Get-Process -Id $ReceiverProcessId
        $tcp = @(Get-NetTCPConnection -OwningProcess $ReceiverProcessId -State Established -ErrorAction SilentlyContinue |
            Where-Object RemoteAddress -eq $TabletAddress)
        if ($tcp.Count -eq 0) { throw 'No established tablet-to-receiver TCP control connection.' }
        $record.ControlConnections = $tcp.Count
        $stat = ((Invoke-Device @('shell','cat',"/proc/$tabletProcessId/stat")) -join '').Trim()
        $tail = $stat.Substring($stat.LastIndexOf(')')+2).Split(' ',[StringSplitOptions]::RemoveEmptyEntries)
        $ticks = [long]$tail[11]+[long]$tail[12]
        $windowsCpu = $receiver.TotalProcessorTime.TotalSeconds
        $cpuElapsed = $timer.Elapsed.TotalSeconds
        if ($null -ne $previousElapsed) {
            $delta = $cpuElapsed-$previousElapsed
            $record.TabletCpuCorePercent = 100*($ticks-$previousTabletTicks)/($clockTicks*$delta)
            $record.WindowsCpuCorePercent = 100*($windowsCpu-$previousWindowsCpu)/$delta
        }
        $previousTabletTicks=$ticks
        $previousWindowsCpu=$windowsCpu
        $previousElapsed=$cpuElapsed
        $record.WindowsWorkingSetBytes=$receiver.WorkingSet64
        $battery = @(Invoke-Device @('shell','dumpsys','battery'))
        $batteryValues = @{}
        foreach ($line in $battery) {
            if ($line -match '^\s+(level|temperature|status|voltage):\s+(-?\d+)\s*$') { $batteryValues[$Matches[1]]=[int]$Matches[2] }
        }
        $record.BatteryLevel=$batteryValues['level']
        $record.BatteryTemperatureC=$batteryValues['temperature']/10.0
        $record.BatteryStatus=$batteryValues['status']
        $record.Thermal=Read-Thermal
        if ($record.Thermal.Status -ge 3) {
            $failure='Android reports severe thermal throttling; stream stopped.'
            Invoke-Device @('shell','am','force-stop',$Package) | Out-Null
        }
        $record.Camera=Read-Camera
    } catch {
        $errors += $_.Exception.Message
        $failure=$_.Exception.Message
    }
    try {
        Invoke-Device @('shell','uiautomator','dump','/data/local/tmp/droidcam-watch-ui.xml') | Out-Null
        [xml]$ui=(Invoke-Device @('shell','cat','/data/local/tmp/droidcam-watch-ui.xml')) -join ''
        $record.AndroidText=@($ui.SelectNodes('//node[@package="'+$Package+'" and @class="android.widget.TextView"]') | ForEach-Object {$_.text})
    } catch { $errors += 'Android UI telemetry: '+$_.Exception.Message }
    try {
        $imagePath=Join-Path $OutputDirectory ('receiver-'+$index.ToString('000')+'.png')
        & (Join-Path $PSScriptRoot 'capture-receiver.ps1') -ProcessId $ReceiverProcessId -OutputPath $imagePath
        $overlay=@(& powershell.exe -NoProfile -File (Join-Path $PSScriptRoot 'read-receiver-overlay.ps1') -ImagePath $imagePath 2>&1)
        if ($LASTEXITCODE -ne 0) { throw ($overlay -join ' ') }
        $record.ReceiverOverlay=@($overlay | ForEach-Object {$_.ToString()})
        $text=$record.ReceiverOverlay -join ' '
        if ($text -match 'decode\s+([0-9O]+[.,][0-9O]+)') {
            $record.DecodedFps=[double]::Parse($Matches[1].Replace('O','0').Replace(',','.'),[Globalization.CultureInfo]::InvariantCulture)
        }
        $record.RtpStalled=$text -match 'RTP\s+stalled'
        if ($text -match '\bAU\s+(\d+)') { $record.LastAccessUnit=[long]$Matches[1] }
        # Keep full preview images only at ten-minute checkpoints; retain every overlay.
        if ($index % [math]::Max(1,[int](600/$IntervalSeconds)) -ne 0) { Remove-Item -LiteralPath $imagePath }
    } catch { $errors += 'Receiver observation: '+$_.Exception.Message }
    $record.Errors=$errors
    [pscustomobject]$record | ConvertTo-Json -Depth 9 -Compress | Add-Content -LiteralPath $samplesPath
    $sampleCount++
    Write-Host ("{0:N1} min | battery {1:N1}C ({2}%) | thermal {3} | decode {4} fps | errors {5}" -f
        ($elapsed/60),$record.BatteryTemperatureC,$record.BatteryLevel,$record.Thermal.Status,$record.DecodedFps,$errors.Count)
    if ($failure -or $elapsed -ge $DurationMinutes*60) { break }
    $index++
    $next=[math]::Min($index*$IntervalSeconds,$DurationMinutes*60)
    while ($timer.Elapsed.TotalSeconds -lt $next) {
        if (Test-Path -LiteralPath (Join-Path $OutputDirectory 'stop')) { $cancelled=$true; break }
        Start-Sleep -Milliseconds 500
    }
    if ($cancelled) { break }
}

$summary=[pscustomobject]@{
    StartedUtc=$start.ToString('o'); FinishedUtc=[DateTime]::UtcNow.ToString('o')
    DurationSeconds=$timer.Elapsed.TotalSeconds; RequestedMinutes=$DurationMinutes
    Samples=$sampleCount; DeviceSerial=$DeviceSerial; Package=$Package
    TabletProcessId=$tabletProcessId; ReceiverProcessId=$ReceiverProcessId
    Cancelled=$cancelled; Failure=$failure
    Completed=($null -eq $failure -and -not $cancelled -and $timer.Elapsed.TotalSeconds -ge $DurationMinutes*60)
}
$summary | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $OutputDirectory 'summary.json')
$summary
if ($failure) { throw $failure }
