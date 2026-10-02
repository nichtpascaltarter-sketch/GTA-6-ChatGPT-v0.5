@echo off
setlocal
pushd "%~dp0"
if errorlevel 1 exit /b 1

rem Use the supported Microsoft x64 compiler and Windows SDK toolchain only.
if /i "%VSCMD_ARG_TGT_ARCH%"=="x64" goto build
set "MC_VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%MC_VSWHERE%" (
    echo ERROR: Install Visual Studio Build Tools with Desktop development with C++ and a Windows SDK containing DXC.
    popd
    exit /b 1
)
set "MC_VSROOT="
for /f "usebackq tokens=*" %%I in (`"%MC_VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "MC_VSROOT=%%I"
if not defined MC_VSROOT (
    echo ERROR: No Microsoft C++ x64 toolchain was found.
    popd
    exit /b 1
)
call "%MC_VSROOT%\Common7\Tools\VsDevCmd.bat" -no_logo -arch=x64 -host_arch=x64
if errorlevel 1 (
    popd
    exit /b 1
)

:build
powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\build.ps1" %*
set "MC_RESULT=%ERRORLEVEL%"
popd
exit /b %MC_RESULT%
