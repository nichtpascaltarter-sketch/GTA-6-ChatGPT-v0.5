[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $Executable,
    [string] $Dumpbin = 'dumpbin.exe'
)
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Executable -PathType Leaf)) {
    throw "Executable not found: $Executable"
}
$image = [IO.File]::ReadAllBytes((Resolve-Path -LiteralPath $Executable).Path)
if ($image.Length -lt 64 -or $image[0] -ne 77 -or $image[1] -ne 90) {
    throw 'The executable does not have a DOS/PE image header.'
}
$peOffset = [BitConverter]::ToUInt32($image, 60)
if ($peOffset + 26 -gt $image.Length -or
    [BitConverter]::ToUInt32($image, $peOffset) -ne 0x00004550 -or
    [BitConverter]::ToUInt16($image, $peOffset + 4) -ne 0x8664 -or
    [BitConverter]::ToUInt16($image, $peOffset + 24) -ne 0x20b) {
    throw 'The executable is not a Windows x64 PE image.'
}
$imports = & $Dumpbin /nologo /dependents $Executable
if ($LASTEXITCODE -ne 0) { throw 'dumpbin import inspection failed.' }
$libraries = @($imports | ForEach-Object {
    if ($_ -match '^\s+([A-Za-z0-9_.-]+\.dll)\s*$') { $Matches[1].ToLowerInvariant() }
} | Sort-Object -Unique)
if ($libraries.Count -eq 0) { throw 'No imports were parsed; the import audit cannot validate this binary.' }
# Only inbox Windows APIs are permitted. Static /MT must remove C/C++ runtime
# DLL imports, and DXC/DXIL/D3DCompiler are build tools rather than runtime imports.
$allowed = @('kernel32.dll', 'kernelbase.dll', 'ntdll.dll', 'user32.dll',
    'gdi32.dll', 'shell32.dll', 'advapi32.dll', 'ole32.dll', 'oleaut32.dll',
    'combase.dll', 'shlwapi.dll', 'd3d12.dll', 'dxgi.dll', 'xinput9_1_0.dll',
    'avrt.dll', 'winmm.dll', 'mmdevapi.dll', 'propsys.dll', 'version.dll')
$unexpected = @($libraries | Where-Object { $_ -notin $allowed })
if ($unexpected.Count -gt 0) {
    throw "Unsupported runtime imports: $($unexpected -join ', ')"
}
$report = "Windows x64 PE; inbox Windows imports only:`r`n" + ($libraries -join "`r`n")
Set-Content -Encoding Ascii -Path "$Executable.imports.txt" -Value $report
Write-Host $report
