[CmdletBinding()]
param(
    [ValidatePattern('^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$')]
    [string] $Repository = 'nichtpascaltarter-sketch/GTA-6-ChatGPT-v0.5',
    [string] $OutputDirectory = (Join-Path (Split-Path -Parent $PSScriptRoot) 'build\warp-comparison'),
    [ValidateRange(180, 900)] [int] $TimeBudgetSeconds = 540
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
if (-not $env:GITHUB_TOKEN) { throw 'GITHUB_TOKEN with Actions artifact read access is required.' }
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
if ((Test-Path -LiteralPath $OutputDirectory) -and @(Get-ChildItem -LiteralPath $OutputDirectory -Force).Count) {
    throw 'Use an empty output directory so earlier comparison evidence cannot be mistaken for this run.'
}
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$cacheDirectory = Join-Path ([IO.Path]::GetTempPath()) ('MeridianCoast-compare-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $cacheDirectory | Out-Null
$variants = @(
    [ordered] @{
        label = 'A'; revision = '88a2cf6e24f3c7037581142915f757d8a49fa5e0'; artifactId = 11202656698L
        sha256 = '93e6fc058d80793d39eb09e7f61c61d7317b3d2e745a7884c4bbd6fff8081897'
    },
    [ordered] @{
        label = 'B'; revision = '2acd647a877b2062e79580c90c36da90eb4d1cdf'; artifactId = 11203285505L
        sha256 = '911b0c5aa17930bdb92ee49c4a94bcbfb687eb03bc2cf52e7c016159efecf7d7'
    }
)
$headers = @{
    Authorization = "Bearer $env:GITHUB_TOKEN"
    Accept = 'application/vnd.github+json'
    'X-GitHub-Api-Version' = '2022-11-28'
    'User-Agent' = 'MeridianCoast-WARP-comparison'
}
$runs = [Collections.Generic.List[object]]::new()
$budget = [Diagnostics.Stopwatch]::new()

function Invoke-ComparisonRun {
    param([string] $Name, [int] $VariantIndex, [int] $Frames, [string] $Kind)
    $variant = $variants[$VariantIndex]
    $directory = Join-Path $OutputDirectory $Name
    New-Item -ItemType Directory -Path $directory | Out-Null
    $executable = Join-Path $directory 'MeridianCoast.exe'
    Copy-Item -LiteralPath (Join-Path $cacheDirectory "$($variant.label).exe") -Destination $executable
    try {
        & (Join-Path $PSScriptRoot 'smoke.ps1') -Executable $executable -Scene city -Frames $Frames -MsaaLimit 4 -TimeoutSeconds 180
        $report = Get-Content -LiteralPath (Join-Path $directory 'smoke-msaa4-report.json') -Raw | ConvertFrom-Json
        if ($report.width -ne 960 -or $report.height -ne 541 -or $report.sampleCount -ne 4 -or
            $report.renderTargetFormat -ne 'R16G16B16A16_FLOAT' -or
            $report.reportedAdapter -ne 'Microsoft Basic Render Driver' -or
            $report.executableSha256 -ne $variant.sha256) {
            throw "Comparison run $Name did not use the pinned executable and fixed rendering settings."
        }
        $runs.Add([ordered] @{
            name = $Name; kind = $Kind; variant = $variant.label; frames = $Frames
            elapsedSeconds = $report.elapsedSeconds
            report = "$Name/smoke-msaa4-report.json"
            screenshot = "$Name/smoke-msaa4.bmp"
            screenshotSha256 = $report.screenshotSha256
        })
    } finally {
        Remove-Item -LiteralPath $executable -Force
    }
}

try {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    foreach ($variant in $variants) {
        $artifactUri = "https://api.github.com/repos/$Repository/actions/artifacts/$($variant.artifactId)"
        $metadata = Invoke-RestMethod -Uri $artifactUri -Headers $headers
        if ($metadata.expired -or $metadata.name -ne 'MeridianCoast-Windows-x64-Release' -or
            $metadata.workflow_run.head_sha -ne $variant.revision) {
            throw "Artifact $($variant.artifactId) is expired or does not match its pinned Release revision."
        }
        $zipPath = Join-Path $cacheDirectory "$($variant.label).zip"
        # Redirect authorization is not preserved. Only the API request gets the
        # token; GitHub's temporary download URL is never printed or persisted.
        Invoke-WebRequest -Uri "$artifactUri/zip" -Headers $headers -OutFile $zipPath
        $archive = [IO.Compression.ZipFile]::OpenRead($zipPath)
        try {
            $entries = @($archive.Entries | Where-Object { $_.FullName -eq 'MeridianCoast.exe' })
            if ($entries.Count -ne 1) { throw 'The artifact must contain exactly one root MeridianCoast.exe.' }
            $destination = Join-Path $cacheDirectory "$($variant.label).exe"
            $inputStream = $entries[0].Open()
            try {
                $outputStream = [IO.File]::Create($destination)
                try { $inputStream.CopyTo($outputStream) } finally { $outputStream.Dispose() }
            } finally { $inputStream.Dispose() }
        } finally { $archive.Dispose() }
        if ((Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash.ToLowerInvariant() -ne $variant.sha256) {
            throw "Artifact $($variant.artifactId) executable does not match its pinned SHA256."
        }
    }
    $budget.Start()
    Invoke-ComparisonRun -Name '00-warmup-A' -VariantIndex 0 -Frames 8 -Kind warmup
    Invoke-ComparisonRun -Name '01-warmup-B' -VariantIndex 1 -Frames 8 -Kind warmup
    Invoke-ComparisonRun -Name '02-A-120' -VariantIndex 0 -Frames 120 -Kind measured
    Invoke-ComparisonRun -Name '03-B-120' -VariantIndex 1 -Frames 120 -Kind measured
    Invoke-ComparisonRun -Name '04-B-120' -VariantIndex 1 -Frames 120 -Kind measured
    Invoke-ComparisonRun -Name '05-A-120' -VariantIndex 0 -Frames 120 -Kind measured

    $summaries = @()
    foreach ($variant in $variants) {
        $values = @($runs | Where-Object { $_.variant -eq $variant.label -and $_.frames -eq 120 } |
            ForEach-Object { [double] $_.elapsedSeconds } | Sort-Object)
        $median = ($values[0] + $values[1]) / 2
        $summaries += [ordered] @{
            variant = $variant.label; median120Seconds = [Math]::Round($median, 4)
            minimum120Seconds = $values[0]; maximum120Seconds = $values[1]
            spread120Percent = [Math]::Round(100 * ($values[1] - $values[0]) / $median, 3)
            elapsed360Seconds = $null; estimatedAdditionalFrameMilliseconds = $null
        }
    }
    $estimatedPairSeconds = 3 * ($summaries[0].median120Seconds + $summaries[1].median120Seconds) + 30
    $longPairStatus = 'skipped: estimated pair would exceed the requested comparison time budget'
    if ($budget.Elapsed.TotalSeconds + $estimatedPairSeconds -le $TimeBudgetSeconds) {
        Invoke-ComparisonRun -Name '06-B-360' -VariantIndex 1 -Frames 360 -Kind slope
        Invoke-ComparisonRun -Name '07-A-360' -VariantIndex 0 -Frames 360 -Kind slope
        $longPairStatus = 'completed'
        foreach ($summary in $summaries) {
            $longRun = @($runs | Where-Object { $_.variant -eq $summary.variant -and $_.frames -eq 360 })[0]
            $summary.elapsed360Seconds = $longRun.elapsedSeconds
            $summary.estimatedAdditionalFrameMilliseconds = [Math]::Round(
                1000 * ($longRun.elapsedSeconds - $summary.median120Seconds) / 240, 4)
        }
    }
    $budget.Stop()
    [ordered] @{
        schemaVersion = 1; repository = $Repository; variants = $variants
        environment = [ordered] @{
            os = [Environment]::OSVersion.VersionString
            logicalProcessors = [Environment]::ProcessorCount
            runnerImage = $env:ImageOS; runnerImageVersion = $env:ImageVersion
        }
        settings = [ordered] @{
            scene = 'city'; adapter = 'Microsoft Basic Render Driver'; width = 960; height = 541
            renderTargetFormat = 'R16G16B16A16_FLOAT'; sampleCount = 4
            warmupFrames = 8; measuredOrder = 'A/B/B/A'; longPairOrder = 'B/A'
        }
        comparisonElapsedSeconds = [Math]::Round($budget.Elapsed.TotalSeconds, 3)
        launchElapsedSecondsTotal = [Math]::Round(($runs | Measure-Object -Property elapsedSeconds -Sum).Sum, 3)
        longPairStatus = $longPairStatus; runs = $runs.ToArray(); summaries = $summaries
        median120ChangePercent = [Math]::Round(100 * ($summaries[1].median120Seconds / $summaries[0].median120Seconds - 1), 3)
        limitations = @(
            'Launch-to-exit intervals include process startup, initial world upload and rendering; they are not GPU frame timestamps.',
            'The additional-frame slope is (one 360-frame launch minus the median 120-frame launch) divided by 240. Startup and scene evolution may differ between launches.',
            'Two measured 120-frame launches per version and one optional 360-frame launch provide limited evidence of variance, not a statistical performance guarantee.',
            'Both pinned binaries run on one hosted Windows machine through software WARP. These measurements do not establish RTX 4070 performance or hardware DXR behavior.'
        )
    } | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath (Join-Path $OutputDirectory 'comparison.json') -Encoding Ascii
    Write-Host "Pinned WARP comparison completed. Reports and captures: $OutputDirectory"
} finally {
    $budget.Stop()
    Remove-Item -LiteralPath $cacheDirectory -Recurse -Force
}
