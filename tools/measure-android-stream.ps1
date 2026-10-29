param(
    [ValidatePattern('^[A-Za-z0-9_.]+$')]
    [string]$Package = 'dev.droidcamclone.app',
    [ValidatePattern('^[A-Za-z0-9_.:-]+$')]
    [string]$DeviceSerial,
    [ValidateRange(5, 60)]
    [int]$SampleSeconds = 10,
    [ValidateRange(1, 60)]
    [int]$Samples = 3,
    [string]$OutputPath,
    [switch]$AllowDebug
)

$ErrorActionPreference = 'Stop'
$adbPrefix = @()
if ($DeviceSerial) { $adbPrefix = @('-s', $DeviceSerial) }

function Invoke-Adb([string[]]$Arguments) {
    $output = @(& adb @adbPrefix @Arguments 2>&1)
    if ($LASTEXITCODE -ne 0) { throw ($output -join [Environment]::NewLine) }
    $output | ForEach-Object { $_.ToString() }
}

function Get-CaptureSnapshot {
    $lines = @(Invoke-Adb @('shell', 'dumpsys', 'media.camera'))
    $active = $lines | Select-String ('^\(Camera ID: ([^,]+),.*Client Package Name: ' + [regex]::Escape($Package) + ',') | Select-Object -First 1
    if (-not $active) { throw "$Package does not own an active camera" }
    $cameraId = $active.Matches[0].Groups[1].Value
    $start = ($lines | Select-String ('^== Camera device ' + [regex]::Escape($cameraId) + ' ') | Select-Object -First 1).LineNumber - 1
    $consumer = ''
    $width = 0
    $height = 0
    $streams = @()
    for ($index = $start + 1; $index -lt $lines.Count; $index++) {
        $line = $lines[$index]
        if ($line -match '^== ') { break }
        if ($line -match 'Consumer name: (.+)') { $consumer = $Matches[1].Trim() }
        if ($line -match 'Dims: (\d+) x (\d+)') { $width = [int]$Matches[1]; $height = [int]$Matches[2] }
        if ($line -match 'Frames produced: (\d+), last timestamp: (\d+) ns') {
            $streams += [pscustomobject]@{
                Consumer = $consumer
                Width = $width
                Height = $height
                Frames = [long]$Matches[1]
                TimestampNs = [long]$Matches[2]
            }
        }
    }
    [pscustomobject]@{ CameraId = $cameraId; Streams = $streams }
}

function Get-Temperatures {
    $lines = @(Invoke-Adb @('shell', 'dumpsys', 'thermalservice'))
    $source = 'cached'
    $header = $lines | Select-String '^Current temperatures from HAL:' | Select-Object -First 1
    if ($header) {
        $lines = @($lines | Select-Object -Skip $header.LineNumber)
        $end = $lines | Select-String '^Current cooling devices' | Select-Object -First 1
        if ($end) { $lines = @($lines | Select-Object -First ($end.LineNumber - 1)) }
        $source = 'current HAL'
    }
    @($lines |
        Select-String 'Temperature\{mValue=([^,]+), mType=([0-3]), mName=([^,]+), mStatus=(\d+)' |
        ForEach-Object {
            [pscustomobject]@{
                Name = $_.Matches[0].Groups[3].Value
                Type = [int]$_.Matches[0].Groups[2].Value
                Celsius = [double]::Parse($_.Matches[0].Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
                Status = [int]$_.Matches[0].Groups[4].Value
                Source = $source
            }
        })
}

$packageDump = @(Invoke-Adb @('shell', 'dumpsys', 'package', $Package))
$flags = $packageDump | Select-String '^\s+pkgFlags=' | Select-Object -First 1
if (-not $flags) { throw "Cannot inspect build flags for $Package" }
$debuggable = $flags.Line -match '\bDEBUGGABLE\b'
if ($debuggable -and -not $AllowDebug) {
    throw 'Debug APK detected. Install assembleBenchmark output, or explicitly pass -AllowDebug for an A/B control.'
}
$processId = ((Invoke-Adb @('shell', 'pidof', $Package)) -join '').Trim()
if ($processId -notmatch '^\d+$') { throw "Expected one running process for $Package" }
if (-not $OutputPath) {
    $OutputPath = Join-Path $env:TEMP ('droidcam-stream-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.json')
}
$OutputPath = [IO.Path]::GetFullPath($OutputPath)
$captureBefore = Get-CaptureSnapshot
$temperaturesBefore = Get-Temperatures
$counters = @()

for ($index = 0; $index -lt $Samples; $index++) {
    $remotePath = '/data/local/tmp/droidcam-counter-' + [guid]::NewGuid().ToString('N') + '.txt'
    try {
        $profilerMessages = @(Invoke-Adb @('shell', 'simpleperf', 'stat', '--app', $Package, '--duration', "$SampleSeconds",
            '-e', 'task-clock,cpu-cycles,instructions', '-o', $remotePath))
        $report = (Invoke-Adb @('shell', 'cat', $remotePath)) -join [Environment]::NewLine
        $values = @{}
        foreach ($match in [regex]::Matches($report, '(?m)^\s*([\d,.]+)(?:\(ms\))?\s+(task-clock|cpu-cycles|instructions)\s')) {
            $values[$match.Groups[2].Value] = [double]::Parse(
                $match.Groups[1].Value.Replace(',', ''), [Globalization.CultureInfo]::InvariantCulture)
        }
        $durationMatch = [regex]::Match($report, 'Total test time:\s*([\d.]+) seconds')
        if ($values.Count -ne 3 -or -not $durationMatch.Success -or $values['task-clock'] -le 0) {
            throw "Incomplete simpleperf counters. A profileable benchmark APK and device PMU access are required. $report"
        }
        $seconds = [double]::Parse($durationMatch.Groups[1].Value, [Globalization.CultureInfo]::InvariantCulture)
        $counters += [pscustomobject]@{
            DurationSeconds = $seconds
            TaskClockMs = $values['task-clock']
            CpuCorePercent = 100 * $values['task-clock'] / (1000 * $seconds)
            CpuCycles = $values['cpu-cycles']
            Instructions = $values['instructions']
            AverageClockGHz = $values['cpu-cycles'] / ($values['task-clock'] * 1e6)
            InstructionsPerSecond = $values['instructions'] / $seconds
            RawReport = $report
            ProfilerMessages = $profilerMessages
        }
        Write-Host ("Sample {0}/{1}: {2:N1}% CPU/core, {3:N2} GHz, {4:N1} M instructions/s" -f
            ($index + 1), $Samples, $counters[-1].CpuCorePercent, $counters[-1].AverageClockGHz,
            ($counters[-1].InstructionsPerSecond / 1e6))
    } finally {
        try { Invoke-Adb @('shell', 'rm', '--', $remotePath) | Out-Null } catch { Write-Verbose $_ }
    }
}

if ((((Invoke-Adb @('shell', 'pidof', $Package)) -join '').Trim()) -ne $processId) {
    throw 'App process changed during measurement; discard this run.'
}
$captureAfter = Get-CaptureSnapshot
if ($captureAfter.CameraId -ne $captureBefore.CameraId) { throw 'Camera changed during measurement; discard this run.' }
$streamRates = @($captureAfter.Streams | ForEach-Object {
    $current = $_
    $previous = $captureBefore.Streams | Where-Object Consumer -eq $current.Consumer | Select-Object -First 1
    $rate = $null
    if ($previous) {
        $delta = $current.TimestampNs - $previous.TimestampNs
        $rate = if ($delta -gt 0) { ($current.Frames - $previous.Frames) * 1e9 / $delta } else { 0 }
    }
    [pscustomobject]@{ Consumer = $current.Consumer; Width = $current.Width; Height = $current.Height; Fps = $rate }
})
$result = [pscustomobject]@{
    Time = (Get-Date -Format o)
    Package = $Package
    ProcessId = $processId
    Debuggable = $debuggable
    CameraId = $captureAfter.CameraId
    Counters = $counters
    Streams = $streamRates
    TemperatureBefore = $temperaturesBefore
    TemperatureAfter = Get-Temperatures
}
$result | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $OutputPath
Write-Host "Saved $OutputPath"
$result
