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
$headers = & $Dumpbin /nologo /headers $Executable
if ($LASTEXITCODE -ne 0) { throw 'dumpbin header inspection failed.' }
if (($headers -join "`n") -notmatch '(?im)8664 machine \(x64\)') {
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
