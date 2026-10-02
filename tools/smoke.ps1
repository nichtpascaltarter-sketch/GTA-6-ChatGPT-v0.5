[CmdletBinding()]
param(
    [string] $Executable = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Release\MeridianCoast.exe'),
    [ValidateRange(1, 3600)] [int] $TimeoutSeconds = 180,
    [ValidateRange(1, 10000)] [int] $Frames = 120,
    [ValidateSet('city', 'coast', 'wetland', 'suburbs', 'rural', 'drive', 'night', 'storm', 'cinematic', 'lifecycle')]
    [string] $Scene = 'city'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if ($Scene -eq 'lifecycle' -and $Frames -lt 91) {
    throw 'The lifecycle scene needs at least 91 frames to complete resize and fullscreen transitions.'
}
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$directory = Split-Path -Parent $Executable
$prefix = if ($Scene -eq 'city') { 'smoke' } else { "smoke-$Scene" }
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
$exitCode = -1
$startedAt = [DateTime]::UtcNow
try {
    # The launch directory contains exactly one file. Build products, external
    # shaders, and neighboring DLLs cannot silently satisfy runtime dependencies.
    Copy-Item -LiteralPath $Executable -Destination $isolatedExecutable
    $process = Start-Process -FilePath $isolatedExecutable -WorkingDirectory $runDirectory -PassThru -NoNewWindow `
        -ArgumentList @('--smoke', '--warp', '--scene', $Scene, '--frames', "$Frames", '--screenshot', ('"' + $screenshot + '"')) `
        -RedirectStandardOutput $stdout -RedirectStandardError $stderr
    $null = $process.Handle
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        $process.Kill()
        $process.WaitForExit()
        throw "WARP smoke test exceeded $TimeoutSeconds seconds."
    }
    $process.Refresh()
    $exitCode = $process.ExitCode
} finally {
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
if (-not (Test-Path $sessionCopy -PathType Leaf)) { throw 'Smoke test did not produce a current session log.' }
$sessionText = [IO.File]::ReadAllText($sessionCopy)
$frameMatch = [regex]::Match($sessionText, 'Exit 0 after ([0-9]+) frames')
if (-not $frameMatch.Success -or [int] $frameMatch.Groups[1].Value -ne $Frames) {
    throw 'The session log does not confirm the requested number of rendered frames.'
}
if ($Scene -eq 'lifecycle') {
    foreach ($transition in @('Lifecycle: resize at frame 30', 'Lifecycle: fullscreen at frame 60', 'Lifecycle: windowed at frame 90')) {
        if (-not $sessionText.Contains($transition)) {
            throw "The lifecycle session did not confirm: $transition"
        }
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
    originalFilename = $fileName
    singleExeDirectory = $true
    windowTransitionsVerified = ($Scene -eq 'lifecycle')
    reportedAdapter = $adapterMatch.Groups[1].Value
    requestedFrames = $Frames
    renderedFrames = [int] $frameMatch.Groups[1].Value
    exitCode = $exitCode
    width = $width
    height = $height
    sampledColors = $colors.Count
    executableSha256 = (Get-FileHash -Algorithm SHA256 $Executable).Hash.ToLowerInvariant()
    screenshotSha256 = (Get-FileHash -Algorithm SHA256 $screenshot).Hash.ToLowerInvariant()
} | ConvertTo-Json | Set-Content -Encoding Ascii -Path (Join-Path $directory "$prefix-report.json")
Write-Host "WARP $Scene smoke passed: $Frames frames; $width x $height screenshot; $($colors.Count) sampled colors."
