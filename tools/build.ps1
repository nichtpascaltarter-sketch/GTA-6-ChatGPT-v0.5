[CmdletBinding()]
param(
    [Parameter(Position = 0)]
    [ValidateSet('Release', 'Debug')]
    [string] $Configuration = 'Release',
    [switch] $SkipTests
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
$output = Join-Path $repo "build\$Configuration"
$generated = Join-Path $output 'generated'
$objects = Join-Path $output 'obj'

function Invoke-Native {
    param([string] $Program, [string[]] $Arguments)
    & $Program @Arguments
    if ($LASTEXITCODE -ne 0) {
        throw "$Program failed with exit code $LASTEXITCODE."
    }
}

function Find-SdkDxc {
    $roots = @()
    if ($env:WindowsSdkDir) { $roots += $env:WindowsSdkDir }
    $roots += (Join-Path ${env:ProgramFiles(x86)} 'Windows Kits\10')
    foreach ($root in ($roots | Select-Object -Unique)) {
        $bin = Join-Path $root 'bin'
        if (-not (Test-Path $bin)) { continue }
        $versions = @(Get-ChildItem $bin -Directory |
            Where-Object { $_.Name -match '^\d+\.\d+\.\d+\.\d+$' } |
            Sort-Object { [version] $_.Name } -Descending)
        foreach ($version in $versions) {
            $candidate = Join-Path $version.FullName 'x64\dxc.exe'
            if (Test-Path $candidate) { return $candidate }
        }
    }
    throw 'DXC was not found in the Windows SDK. Install a current Windows 10/11 SDK with DirectX shader compiler tools.'
}

try {
    $compiler = (Get-Command cl.exe -ErrorAction Stop).Source
    $dumpbin = (Get-Command dumpbin.exe -ErrorAction Stop).Source
    if ($env:VSCMD_ARG_TGT_ARCH -ne 'x64') {
        throw 'Run build.bat to select the Microsoft x64 developer environment.'
    }
    $dxc = Find-SdkDxc
    # Every invocation is a full rebuild. A failed build cannot leave a stale
    # executable or validation report masquerading as the current revision.
    if (Test-Path -LiteralPath $output) { Remove-Item -LiteralPath $output -Recurse -Force }
    New-Item -ItemType Directory -Force $output, $generated, $objects | Out-Null

    $shaders = @(
        @{ Source = 'world'; Stage = 'vs'; Profile = 'vs_6_0'; Entry = 'VSMain'; Define = $null },
        @{ Source = 'world'; Stage = 'ps'; Profile = 'ps_6_0'; Entry = 'PSMain'; Define = $null },
        @{ Source = 'world'; Stage = 'far_ps'; Profile = 'ps_6_0'; Entry = 'PSMain'; Define = 'FAR_GEOMETRY=1' },
        @{ Source = 'world'; Stage = 'rt_ps'; Profile = 'ps_6_5'; Entry = 'PSMain'; Define = 'ENABLE_RAYTRACING=1' },
        @{ Name = 'shadow_vs'; Source = 'world'; Stage = 'vs'; Profile = 'vs_6_0'; Entry = 'VSShadow'; Define = $null },
        @{ Source = 'ui'; Stage = 'vs'; Profile = 'vs_6_0'; Entry = 'VSMain'; Define = $null },
        @{ Source = 'ui'; Stage = 'ps'; Profile = 'ps_6_0'; Entry = 'PSMain'; Define = $null },
        @{ Source = 'sky'; Stage = 'vs'; Profile = 'vs_6_0'; Entry = 'VSMain'; Define = $null },
        @{ Source = 'sky'; Stage = 'ps'; Profile = 'ps_6_0'; Entry = 'PSMain'; Define = $null },
        @{ Source = 'post'; Stage = 'ps'; Profile = 'ps_6_0'; Entry = 'PSMain'; Define = $null }
    )
    foreach ($shader in $shaders) {
        $name = if ($shader.ContainsKey('Name')) { $shader.Name } else { "$($shader.Source)_$($shader.Stage)" }
        $arguments = @('-T', $shader.Profile, '-E', $shader.Entry,
            '-HV', '2021', '-Ges', '-WX', '-O3', '-Qstrip_debug', '-Qstrip_reflect',
            '-I', (Join-Path $repo 'shaders'),
            '-Fh', (Join-Path $generated "$name.h"), '-Vn', "g_$name",
            '-Fo', (Join-Path $generated "$name.dxil"),
            (Join-Path $repo "shaders\$($shader.Source).hlsl"))
        if ($shader.Define) { $arguments += @('-D', $shader.Define) }
        Write-Host "Compiling shader $name ($($shader.Profile))"
        Invoke-Native $dxc $arguments
    }

    $common = @('/nologo', '/std:c++20', '/EHsc', '/MT', '/W4', '/permissive-',
        '/Zc:__cplusplus', '/Zc:inline', '/utf-8', '/DWIN32_LEAN_AND_MEAN',
        '/DNOMINMAX', '/DUNICODE', '/D_UNICODE', '/D_WIN32_WINNT=0x0A00',
        "/I$generated")
    if ($Configuration -eq 'Release') {
        $common += @('/O2', '/GL', '/DNDEBUG')
        $linkConfiguration = @('/LTCG', '/OPT:REF', '/OPT:ICF')
    } else {
        $common += @('/Od', '/Zi', '/RTC1', '/DMC_DEBUG=1')
        $linkConfiguration = @('/DEBUG:FULL', '/INCREMENTAL:NO')
    }
    $systemLibraries = @('kernel32.lib', 'user32.lib', 'gdi32.lib', 'shell32.lib',
        'd3d12.lib', 'dxgi.lib', 'dxguid.lib', 'xinput9_1_0.lib', 'ole32.lib',
        'uuid.lib', 'avrt.lib')
    $sourceNames = @('main', 'world', 'world_geometry', 'world_streamer', 'game', 'pedestrians', 'visuals', 'renderer', 'audio')
    $sources = @($sourceNames | ForEach-Object { Join-Path $repo "src\$_.cpp" })
    $executable = Join-Path $output 'MeridianCoast.exe'
    Write-Host "Building $Configuration x64 executable with static C/C++ runtime"
    Invoke-Native $compiler ($common + $sources + @("/Fo$objects\", "/Fd$output\MeridianCoast-compile.pdb",
        "/Fe$executable", '/link', '/SUBSYSTEM:WINDOWS', '/MACHINE:X64',
        '/DYNAMICBASE', '/NXCOMPAT', '/HIGHENTROPYVA', "/PDB:$output\MeridianCoast.pdb") +
        $linkConfiguration + $systemLibraries)

    & (Join-Path $PSScriptRoot 'audit-imports.ps1') -Executable $executable -Dumpbin $dumpbin
    if (-not $SkipTests) {
        $testSets = @(
            @{ Name = 'world'; Sources = @('world', 'world_geometry') },
            @{ Name = 'market'; Sources = @('world', 'world_geometry') },
            @{ Name = 'world_streamer'; Sources = @('world', 'world_geometry', 'world_streamer') },
            @{ Name = 'world_lod_geometry'; Sources = @('world', 'world_geometry') },
            @{ Name = 'world_lod_streaming'; Sources = @('world', 'world_geometry', 'world_streamer') },
            @{ Name = 'render_visibility'; Sources = @() },
            @{ Name = 'render_timing'; Sources = @() },
            @{ Name = 'game'; Sources = @('game', 'pedestrians', 'world', 'world_geometry', 'visuals') },
            @{ Name = 'pedestrian'; Sources = @('game', 'pedestrians', 'world', 'world_geometry', 'visuals') },
            @{ Name = 'workshop_lighting'; Sources = @('game', 'pedestrians', 'world', 'world_geometry', 'visuals') },
            @{ Name = 'pedestrian_visuals'; Sources = @('game', 'pedestrians', 'world', 'world_geometry', 'visuals') },
            @{ Name = 'audio'; Sources = @('audio') },
            @{ Name = 'world_audio'; Sources = @() },
            @{ Name = 'audio_output'; Sources = @() },
            @{ Name = 'world_audio_scene'; Sources = @('game', 'pedestrians', 'world', 'world_geometry', 'visuals') },
            @{ Name = 'world_audio_integration'; Sources = @('game', 'pedestrians', 'world', 'world_geometry', 'visuals') },
            @{ Name = 'cinematics'; Sources = @('world', 'world_geometry') }
        )
        foreach ($test in $testSets) {
            $testObjects = Join-Path $objects $test.Name
            New-Item -ItemType Directory -Force $testObjects | Out-Null
            $testSources = @((Join-Path $repo "tests\$($test.Name)_tests.cpp")) +
                @($test.Sources | ForEach-Object { Join-Path $repo "src\$_.cpp" })
            $testExecutable = Join-Path $output "$($test.Name)_tests.exe"
            Write-Host "Building and running $($test.Name) tests"
            # Keep assertion-based tests active in both build configurations.
            $testFlags = @($common | Where-Object { $_ -ne '/DNDEBUG' })
            Invoke-Native $compiler ($testFlags + $testSources + @("/Fo$testObjects\",
                "/Fd$testObjects\compile.pdb", "/Fe$testExecutable", '/link',
                '/SUBSYSTEM:CONSOLE', '/MACHINE:X64', "/PDB:$testObjects\tests.pdb") +
                $linkConfiguration + $systemLibraries)
            Invoke-Native $testExecutable @()
        }
    }
    $hash = (Get-FileHash -Algorithm SHA256 $executable).Hash.ToLowerInvariant()
    Set-Content -Encoding Ascii -Path "$executable.sha256" -Value "$hash  MeridianCoast.exe"
    Write-Host "Built $executable"
    Write-Host "SHA256 $hash"
} catch {
    Write-Error $_
    exit 1
}
