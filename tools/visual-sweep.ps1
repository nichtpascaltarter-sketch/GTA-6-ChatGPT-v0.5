[CmdletBinding()]
param(
    [string] $Executable = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\Release\MeridianCoast.exe'),
    [ValidateRange(1, 10000)] [int] $Frames = 8,
    [ValidateRange(1, 3600)] [int] $TimeoutSeconds = 120,
    [switch] $RequireSpatialAudio
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$Executable = (Resolve-Path -LiteralPath $Executable).Path
$directory = Split-Path -Parent $Executable
$reports = @()
foreach ($scene in @('night', 'storm', 'coast', 'suburbs', 'wetland', 'rural', 'drive', 'cinematic', 'portrait', 'vehicle', 'map', 'boat', 'aircraft', 'rescue', 'survey', 'passenger-car', 'passenger-bike', 'passenger-boat', 'passenger-plane', 'trial', 'trial-run', 'trial-map', 'workshop', 'workshop-day', 'workshop-night', 'workshop-office', 'workshop-door', 'workshop-service', 'residents-carry', 'residents-bench', 'residents-talk', 'residents-startle', 'residents-flee', 'residents-work', 'market-day', 'market-night', 'market-citrus', 'market-tea', 'market-bread', 'police-aim', 'police-fire', 'police-reload')) {
    & (Join-Path $PSScriptRoot 'smoke.ps1') -Executable $Executable -Frames $Frames -Scene $scene -TimeoutSeconds $TimeoutSeconds -RequireTiming -RequireSpatialAudio:$RequireSpatialAudio
    $reports += Get-Content -Raw (Join-Path $directory "smoke-$scene-report.json") | ConvertFrom-Json
}
$reports | ConvertTo-Json -Depth 4 | Set-Content -Encoding Ascii -Path (Join-Path $directory 'visual-sweep.json')
Write-Host "Visual sweep passed for $($reports.Count) scenes. Captures are ready for visual inspection."
