[CmdletBinding()]
param(
    [string] $Executable = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Release\MeridianCoast.exe'),
    [ValidateRange(1, 3600)] [int] $TimeoutSeconds = 180,
    [ValidateRange(1, 10000)] [int] $Frames = 120,
    [ValidateSet('city', 'coast', 'wetland', 'suburbs', 'rural', 'drive', 'night', 'storm', 'cinematic', 'portrait', 'vehicle', 'map', 'boat', 'aircraft', 'rescue', 'survey', 'passenger-car', 'passenger-bike', 'passenger-boat', 'passenger-plane', 'trial', 'trial-run', 'trial-map', 'lifecycle', 'streaming', 'lod')]
    [string] $Scene = 'city',
    [ValidateSet(0, 1, 2, 4)] [int] $MsaaLimit = 0,
    [switch] $RequireTiming,
    [switch] $DisableGpuTimestamps
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($Scene -eq 'lifecycle' -and $Frames -lt 91) {
    throw 'The lifecycle scene needs at least 91 frames to complete resize and fullscreen transitions.'
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$directory = Split-Path -Parent $Executable
$prefix = if ($Scene -eq 'city') { 'smoke' } else { "smoke-$Scene" }
if ($MsaaLimit -gt 0) { $prefix += "-msaa$MsaaLimit" }
if ($DisableGpuTimestamps) { $prefix += '-no-gpu-timestamps' }
$screenshot = Join-Path $directory "$prefix.bmp"
$stdout = Join-Path $directory "$prefix-stdout.log"
$stderr = Join-Path $directory "$prefix-stderr.log"
$sessionCopy = Join-Path $directory "$prefix-session.log"
if (Test-Path $screenshot) { Remove-Item -LiteralPath $screenshot -Force }
if (Test-Path $sessionCopy) { Remove-Item -LiteralPath $sessionCopy -Force }

$fileName = Split-Path -Leaf $Executable
$runDirectory = Join-Path ([IO.Path]::GetTempPath()) ("MeridianCoast-smoke-" + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $runDirectory | Out-Null
$isolatedExecutable = Join-Path $runDirectory $fileName
$process = $null
$processTimer = [Diagnostics.Stopwatch]::new()
$previousMsaaLimit = [Environment]::GetEnvironmentVariable('MERIDIAN_MSAA_LIMIT', 'Process')
$previousGpuTimestamps = [Environment]::GetEnvironmentVariable('MERIDIAN_GPU_TIMESTAMPS', 'Process')
$exitCode = -1
$startedAt = [DateTime]::UtcNow
try {
    # The launch directory contains exactly one file. Build products, external
    # shaders, and neighboring DLLs cannot silently satisfy runtime dependencies.
    Copy-Item -LiteralPath $Executable -Destination $isolatedExecutable
    if ($MsaaLimit -gt 0) { [Environment]::SetEnvironmentVariable('MERIDIAN_MSAA_LIMIT', "$MsaaLimit", 'Process') }
    if ($DisableGpuTimestamps) { [Environment]::SetEnvironmentVariable('MERIDIAN_GPU_TIMESTAMPS', '0', 'Process') }
    $processTimer.Start()
    $process = Start-Process -FilePath $isolatedExecutable -WorkingDirectory $runDirectory -PassThru -NoNewWindow `
        -ArgumentList @('--smoke', '--warp', '--scene', $Scene, '--frames', "$Frames", '--screenshot', ('"' + $screenshot + '"')) `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $null = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "WARP smoke test exceeded $TimeoutSeconds seconds."
    }
    $processTimer.Stop()
    $process.Refresh()
    $exitCode = $process.ExitCode
} finally {
    $processTimer.Stop()
    if ($MsaaLimit -gt 0) { [Environment]::SetEnvironmentVariable('MERIDIAN_MSAA_LIMIT', $previousMsaaLimit, 'Process') }
    if ($DisableGpuTimestamps) { [Environment]::SetEnvironmentVariable('MERIDIAN_GPU_TIMESTAMPS', $previousGpuTimestamps, 'Process') }
    try {
        if ($process) {
            if (-not $process.HasExited) { $process.Kill(); $process.WaitForExit() }
            $process.Dispose()
        }
        $dataDirectory = [Environment]::GetFolderPath('LocalApplicationData')
        if (-not $dataDirectory) { $dataDirectory = [IO.Path]::GetTempPath() }
        $sessionLog = Join-Path $dataDirectory 'MeridianCoast\session.log'
        if ((Test-Path $sessionLog) -and (Get-Item $sessionLog).LastWriteTimeUtc -ge $startedAt.AddSeconds(-1)) {
            Copy-Item -LiteralPath $sessionLog -Destination $sessionCopy -Force
        }
    } finally {
        Remove-Item -LiteralPath $runDirectory -Recurse -Force
    }
}
if ($exitCode -ne 0) {
    if (Test-Path $stderr) { Get-Content $stderr | Write-Host }
    throw "WARP smoke test failed with exit code $exitCode."
}
$stderrText = [IO.File]::ReadAllText($stderr)
$targetMatch = [regex]::Match($stderrText, 'Scene target: ([^;\r\n]+); samples=([0-9]+); requested limit=([0-9]+)')
if (-not $targetMatch.Success) { throw 'The renderer did not report its HDR target and sample count.' }
$actualSamples = [int] $targetMatch.Groups[2].Value
$requestedSamples = [int] $targetMatch.Groups[3].Value
if ($actualSamples -notin @(1, 2, 4) -or $actualSamples -gt $requestedSamples -or
    ($MsaaLimit -gt 0 -and $requestedSamples -ne $MsaaLimit)) {
    throw 'The renderer selected an invalid sample count or did not honor the requested limit.'
}
if (-not (Test-Path $sessionCopy -PathType Leaf)) { throw 'Smoke test did not produce a current session log.' }
$sessionText = [IO.File]::ReadAllText($sessionCopy)
$frameMatch = [regex]::Match($sessionText, 'Exit 0 after ([0-9]+) frames')
if (-not $frameMatch.Success -or [int] $frameMatch.Groups[1].Value -ne $Frames) {
    throw 'The session log does not confirm the requested number of rendered frames.'
}
# Historic pinned binaries used by compare-warp.ps1 predate telemetry. They may
# omit it; a present report is always validated, and current native CI requires it.
$renderTiming = $null
$timingStatus = 'not reported by this binary'
$timingMatches = [regex]::Matches($sessionText, '(?m)^Render timing: (?<metrics>[^\r\n]+)\r?$')
$timingLineCount = [regex]::Matches($sessionText, '(?m)^Render timing:').Count
if ($timingLineCount -ne 0 -or $RequireTiming -or $DisableGpuTimestamps) {
    if ($timingMatches.Count -ne 1 -or $timingLineCount -ne 1) {
        throw 'The session must contain exactly one complete final render timing report.'
    }
    $integerMetrics = @('cpu', 'gpu', 'frequency', 'submitted', 'completed', 'pending', 'samples',
        'gpuSamples', 'invalid', 'latest', 'worldCalls')
    $durationMetrics = @('cpuMean', 'cpuMax', 'waitMean', 'prepareMean', 'recordMean',
        'presentMean', 'gpuMean', 'gpuMax', 'worldMean', 'worldMax')
    $renderTiming = [ordered] @{}
    foreach ($part in $timingMatches[0].Groups['metrics'].Value.Split(';')) {
        $metric = [regex]::Match($part.Trim(), '^([A-Za-z]+)=([0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)$')
        if (-not $metric.Success) { throw 'Malformed render timing metric.' }
        $name = $metric.Groups[1].Value
        if (($name -notin $integerMetrics -and $name -notin $durationMetrics) -or $renderTiming.Contains($name)) {
            throw "Unexpected or duplicate render timing metric: $name"
        }
        if ($name -in $integerMetrics) {
            $renderTiming[$name] = [UInt64]::Parse($metric.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
        } else {
            $value = [double]::Parse($metric.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
            if ([double]::IsNaN($value) -or [double]::IsInfinity($value) -or $value -lt 0) {
                throw "Render timing duration is not finite and nonnegative: $name"
            }
            $renderTiming[$name] = $value
        }
    }
    foreach ($name in ($integerMetrics + $durationMetrics)) {
        if (-not $renderTiming.Contains($name)) { throw "Missing render timing metric: $name" }
    }
    $t = $renderTiming
    if ($t.cpu -ne 1 -or $t.gpu -notin @(0, 1) -or $t.submitted -ne $Frames -or
        $t.completed -ne $Frames -or $t.latest -ne $Frames -or $t.pending -ne 0 -or $t.invalid -ne 0 -or
        $t.samples -ne [Math]::Min(120, $Frames) -or $t.cpuMax -lt $t.cpuMean -or
        ($t.waitMean + $t.prepareMean + $t.recordMean + $t.presentMean) -gt ($t.cpuMean + 0.01) -or
        $t.worldCalls -eq 0 -or $t.worldMax -lt $t.worldMean) {
        throw 'Render timing did not confirm drained, ordered frame samples and valid CPU durations.'
    }
    $timingStatusMatch = [regex]::Match($stderrText, '(?m)^GPU frame timestamps: ([^\r\n]+)\r?$')
    if (-not $timingStatusMatch.Success) { throw 'The renderer did not explain GPU timestamp availability.' }
    $timingStatus = $timingStatusMatch.Groups[1].Value
    if ($t.gpu -eq 1) {
        $enabledStatus = "enabled; frequency=$($t.frequency) Hz; slots=2; history=120"
        if ($DisableGpuTimestamps -or $t.frequency -eq 0 -or $t.gpuSamples -ne $t.samples -or
            $t.gpuMean -le 0 -or $t.gpuMax -lt $t.gpuMean -or $timingStatus -ne $enabledStatus) {
            throw 'GPU timing did not confirm valid matched frame intervals and timestamp frequency.'
        }
    } else {
        if ($t.frequency -ne 0 -or $t.gpuSamples -ne 0 -or $t.gpuMean -ne 0 -or $t.gpuMax -ne 0 -or
            $timingStatus -notmatch '^(disabled by MERIDIAN_GPU_TIMESTAMPS=0|unavailable \(.+\))$') {
            throw 'Unavailable GPU timestamps did not preserve clean CPU-only timing samples.'
        }
        if ($DisableGpuTimestamps -and $timingStatus -ne 'disabled by MERIDIAN_GPU_TIMESTAMPS=0') {
            throw 'The renderer did not acknowledge the forced GPU timestamp disable setting.'
        }
    }
}
$verifiedWindowStates = @()
if ($Scene -eq 'lifecycle') {
    foreach ($transition in @('Lifecycle: resize at frame 30', 'Lifecycle: fullscreen at frame 60', 'Lifecycle: windowed at frame 90')) {
        if (-not $sessionText.Contains($transition)) {
            throw "The lifecycle session did not confirm: $transition"
        }
    }
    foreach ($frame in @(30, 60, 90)) {
        $client = [regex]::Match($sessionText, "Lifecycle verified client: ([0-9]+)x([0-9]+) at frame $frame\b")
        if (-not $client.Success) { throw "The lifecycle session did not verify the window state at frame $frame." }
        $verifiedWindowStates += [ordered] @{
            frame = $frame
            width = [int] $client.Groups[1].Value
            height = [int] $client.Groups[2].Value
        }
    }
}
$streamingPhases = @()
if ($Scene -eq 'streaming') {
    $phasePattern = '(?m)^Streaming phase (?<phase>\d+): center=(?<x>-?\d+),(?<z>-?\d+); epoch=(?<epoch>\d+); chunks=(?<chunks>\d+); uploaded=(?<uploaded>\d+); expected=(?<expected>\d+); retained=(?<retained>\d+); cpuPending=(?<pending>\d+); batches=(?<batches>\d+); fallbacks=(?<fallbacks>\d+); ordinaryWaits=(?<ordinary>\d+); pressureWaits=(?<pressure>\d+); repacks=(?<repacks>\d+); residentBytes=(?<residentBytes>\d+); retiredBytes=(?<retiredBytes>\d+); scheduled=(?<scheduled>\d+); built=(?<built>\d+); installed=(?<installed>\d+)\r?$'
    $phases = [regex]::Matches($sessionText, $phasePattern)
    if ($phases.Count -ne 8 -or [regex]::Matches($sessionText, '(?m)^Streaming phase ').Count -ne 8 -or
        -not $sessionText.Contains('Streaming verified: 8 settled phases; incremental uploads and epoch reset passed')) {
        throw 'The streaming session did not confirm all eight settled phases.'
    }
    $expectedUploads = @(49, 7, 7, 13, 25, 49, 49, 49)
    $expectedX = @(0, 1, 2, 3, 0, 19, 0, 0)
    $expectedZ = @(0, 0, 0, 1, 0, 2, 0, 0)
    $initialEpoch = [UInt64] $phases[0].Groups['epoch'].Value
    for ($index = 0; $index -lt 8; ++$index) {
        $match = $phases[$index]
        $phase = [ordered] @{
            phase = [int] $match.Groups['phase'].Value
            centerX = [int] $match.Groups['x'].Value
            centerZ = [int] $match.Groups['z'].Value
            epoch = [UInt64] $match.Groups['epoch'].Value
            residentChunks = [int] $match.Groups['chunks'].Value
            uploadedChunks = [UInt64] $match.Groups['uploaded'].Value
            expectedUploads = [UInt64] $match.Groups['expected'].Value
            retainedChunks = [UInt64] $match.Groups['retained'].Value
            pendingChunks = [int] $match.Groups['pending'].Value
            pendingBatches = [int] $match.Groups['batches'].Value
            synchronousFallbacks = [UInt64] $match.Groups['fallbacks'].Value
            ordinaryWaits = [UInt64] $match.Groups['ordinary'].Value
            pressureWaits = [UInt64] $match.Groups['pressure'].Value
            repackWaits = [UInt64] $match.Groups['repacks'].Value
            residentBytes = [UInt64] $match.Groups['residentBytes'].Value
            retiredBytes = [UInt64] $match.Groups['retiredBytes'].Value
            scheduledChunks = [UInt64] $match.Groups['scheduled'].Value
            builtChunks = [UInt64] $match.Groups['built'].Value
            installedChunks = [UInt64] $match.Groups['installed'].Value
        }
        $expectedEpoch = if ($index -eq 7) { $initialEpoch + 1 } else { $initialEpoch }
        if ($phase.phase -ne $index -or $phase.centerX -ne $expectedX[$index] -or
            $phase.centerZ -ne $expectedZ[$index] -or $phase.epoch -ne $expectedEpoch -or
            $phase.residentChunks -ne 49 -or $phase.uploadedChunks -ne $expectedUploads[$index] -or
            $phase.expectedUploads -ne $expectedUploads[$index] -or
            $phase.retainedChunks -ne (49 - $expectedUploads[$index]) -or
            $phase.pendingChunks -ne 0 -or $phase.pendingBatches -ne 0 -or
            $phase.ordinaryWaits -ne 0 -or $phase.pressureWaits -ne 0 -or $phase.repackWaits -ne 0) {
            throw "Streaming phase $index did not verify its expected neighborhood, upload delta, or settled resources."
        }
        if ($index -ge 1 -and $index -le 3) {
            if ($phase.synchronousFallbacks -ne $streamingPhases[-1].synchronousFallbacks -or
                $phase.scheduledChunks -ne $expectedUploads[$index] -or
                $phase.builtChunks -ne $expectedUploads[$index] -or
                $phase.installedChunks -ne $expectedUploads[$index]) {
                throw "Streaming phase $index did not generate its new chunks through the background workers."
            }
        }
        $streamingPhases += $phase
    }
}
$lodPhases = @()
if ($Scene -eq 'lod') {
    $lodMatches = [regex]::Matches($sessionText, '(?m)^LOD phase (?<phase>\d+): center=(?<x>-?\d+),(?<z>-?\d+); (?<metrics>[^\r\n]+)\r?$')
    if ($lodMatches.Count -ne 5 -or [regex]::Matches($sessionText, '(?m)^LOD phase ').Count -ne 5 -or
        -not $sessionText.Contains('LOD verified: 5 settled phases; coverage, residency, culling and epoch reset passed')) {
        throw 'The distant-world session did not confirm all five settled phases.'
    }
    $expectedX = @(0, 1, -25, 19, 19)
    $expectedZ = @(0, 0, -5, 2, 2)
    $metricNames = @('epoch', 'detail', 'medium', 'far', 'ready', 'fogEnd', 'cacheBytes', 'fallbackBytes',
        'residentBytes', 'uploaded', 'uploadedDetail', 'uploadedMedium', 'uploadedFar', 'tlasBuilds',
        'rayInstances', 'mainDrawn', 'mainCulled', 'shadowDrawn', 'shadowCulled', 'batches',
        'retiredBytes', 'ordinaryWaits', 'pressureWaits', 'repacks', 'frame')
    for ($index = 0; $index -lt 5; ++$index) {
        $match = $lodMatches[$index]
        $phase = [ordered] @{
            phase = [int] $match.Groups['phase'].Value
            centerX = [int] $match.Groups['x'].Value
            centerZ = [int] $match.Groups['z'].Value
        }
        foreach ($part in $match.Groups['metrics'].Value.Split(';')) {
            $metric = [regex]::Match($part.Trim(), '^([A-Za-z]+)=([0-9]+(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)$')
            if (-not $metric.Success) { throw "Malformed distant-world metric in phase $index." }
            $name = $metric.Groups[1].Value
            if ($name -notin $metricNames -or $phase.Contains($name)) { throw "Unexpected or duplicate distant-world metric: $name" }
            $phase[$name] = if ($name -in @('ready', 'fogEnd')) {
                [double]::Parse($metric.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
            } else {
                [UInt64]::Parse($metric.Groups[2].Value, [Globalization.CultureInfo]::InvariantCulture)
            }
        }
        foreach ($name in $metricNames) { if (-not $phase.Contains($name)) { throw "Missing distant-world metric: $name" } }
        $expectedEpoch = if ($index -eq 0) { $phase.epoch } elseif ($index -eq 4) { $lodPhases[0].epoch + 1 } else { $lodPhases[0].epoch }
        # A one-cell move deliberately retains 15 medium tiles in the cache's
        # hysteresis band; those replace far tiles and remain shadow candidates.
        $expectedMedium = if ($index -eq 1) { 191 } else { 176 }
        $expectedFar = if ($index -eq 1) { 849 } else { 864 }
        $expectedShadow = 49 + $expectedMedium
        if ($phase.phase -ne $index -or $phase.centerX -ne $expectedX[$index] -or
            $phase.centerZ -ne $expectedZ[$index] -or $phase.epoch -ne $expectedEpoch -or
            $phase.detail -ne 49 -or $phase.medium -ne $expectedMedium -or $phase.far -ne $expectedFar -or
            $phase.ready -lt 2000 -or $phase.fogEnd -lt 1980 -or $phase.fogEnd -gt 2000 -or
            $phase.cacheBytes -gt 64MB -or $phase.fallbackBytes -gt 64MB -or
            $phase.residentBytes -eq 0 -or $phase.residentBytes -gt 320MB -or
            $phase.uploaded -ne ($phase.uploadedDetail + $phase.uploadedMedium + $phase.uploadedFar) -or
            $phase.tlasBuilds -ne 0 -or $phase.rayInstances -ne 0 -or
            $phase.mainDrawn -eq 0 -or $phase.mainCulled -eq 0 -or ($phase.mainDrawn + $phase.mainCulled) -ne 1089 -or
            $phase.shadowDrawn -eq 0 -or $phase.shadowCulled -eq 0 -or ($phase.shadowDrawn + $phase.shadowCulled) -ne $expectedShadow -or
            $phase.batches -ne 0 -or $phase.retiredBytes -ne 0 -or $phase.ordinaryWaits -ne 0 -or
            $phase.pressureWaits -ne 0 -or $phase.repacks -ne 0 -or $phase.frame -gt $Frames) {
            throw "LOD phase $index did not verify its expected coverage, residency, culling, or drained resources."
        }
        if ($index -gt 0) {
            $previous = $lodPhases[-1]
            if ($phase.frame -le $previous.frame -or $phase.uploaded -lt $previous.uploaded -or
                $phase.uploadedDetail -lt $previous.uploadedDetail -or
                ($index -eq 1 -and $phase.uploadedDetail - $previous.uploadedDetail -ne 7) -or
                ($index -eq 4 -and $phase.uploadedDetail - $previous.uploadedDetail -ne 49)) {
                throw "LOD phase $index did not preserve detailed tile reuse or epoch reset uploads."
            }
        }
        $lodPhases += $phase
    }
}
$adapterMatch = [regex]::Match($sessionText, 'Adapter: ([^\r\n]+)')
if (-not (Test-Path $screenshot -PathType Leaf)) { throw 'Smoke test did not save a screenshot.' }
$bytes = [IO.File]::ReadAllBytes($screenshot)
if ($bytes.Length -lt 54 -or $bytes[0] -ne 66 -or $bytes[1] -ne 77) {
    throw 'Smoke screenshot is not a BMP image.'
}
$width = [BitConverter]::ToInt32($bytes, 18)
$height = [Math]::Abs([BitConverter]::ToInt32($bytes, 22))
$bits = [BitConverter]::ToUInt16($bytes, 28)
$offset = [BitConverter]::ToUInt32($bytes, 10)
if ($width -lt 320 -or $height -lt 180 -or $bits -notin @(24, 32)) {
    throw "Smoke screenshot has unexpected dimensions or format: $width x $height, $bits bpp."
}
if ($Scene -eq 'lifecycle') {
    $restored = $verifiedWindowStates[-1]
    if ($width -ne $restored.width -or $height -ne $restored.height) {
        throw 'The captured swap-chain dimensions do not match the restored window client area.'
    }
}
$stride = [int64] ([Math]::Floor(($width * $bits + 31) / 32) * 4)
if ($offset + $stride * $height -gt $bytes.Length) { throw 'Smoke screenshot pixel data is truncated.' }
# A successful Present with a completely uniform buffer does not validate rendering.
$colors = [Collections.Generic.HashSet[int]]::new()
$pixelBytes = $bits / 8
for ($y = 0; $y -lt $height; $y += [Math]::Max(1, [int] ($height / 40))) {
    for ($x = 0; $x -lt $width; $x += [Math]::Max(1, [int] ($width / 60))) {
        $index = [int] ($offset + $stride * $y + $pixelBytes * $x)
        $color = [int] $bytes[$index] -bor ([int] $bytes[$index + 1] -shl 8) -bor ([int] $bytes[$index + 2] -shl 16)
        [void] $colors.Add($color)
    }
}
if ($colors.Count -lt 16) { throw "Smoke screenshot is nearly uniform ($($colors.Count) sampled colors)." }
[ordered] @{
    adapter = 'D3D12 WARP'
    scene = $Scene
    renderTargetFormat = $targetMatch.Groups[1].Value
    sampleCount = $actualSamples
    requestedSampleLimit = $requestedSamples
    timingRequired = [bool] $RequireTiming
    gpuTimestampsForcedDisabled = [bool] $DisableGpuTimestamps
    gpuTimestampStatus = $timingStatus
    renderTiming = $renderTiming
    originalFilename = $fileName
    singleExeDirectory = $true
    windowTransitionsVerified = ($Scene -eq 'lifecycle')
    verifiedWindowStates = $verifiedWindowStates
    streamingVerified = ($Scene -eq 'streaming')
    streamingPhases = $streamingPhases
    lodVerified = ($Scene -eq 'lod')
    lodPhases = $lodPhases
    reportedAdapter = $adapterMatch.Groups[1].Value
    requestedFrames = $Frames
    renderedFrames = [int] $frameMatch.Groups[1].Value
    exitCode = $exitCode
    elapsedSeconds = [Math]::Round($processTimer.Elapsed.TotalSeconds, 3)
    width = $width
    height = $height
    sampledColors = $colors.Count
    executableSha256 = (Get-FileHash -Algorithm SHA256 $Executable).Hash.ToLowerInvariant()
    screenshotSha256 = (Get-FileHash -Algorithm SHA256 $screenshot).Hash.ToLowerInvariant()
} | ConvertTo-Json -Depth 4 | Set-Content -Encoding Ascii -Path (Join-Path $directory "$prefix-report.json")
Write-Host "WARP $Scene smoke passed: $Frames frames; $width x $height screenshot; $($colors.Count) sampled colors."
