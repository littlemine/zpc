@echo off

set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
  echo [zpc] vswhere.exe not found at "%VSWHERE%"
  exit /b 1
)

set "VCVARS="
for /f "usebackq delims=" %%I in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -find VC\Auxiliary\Build\vcvarsall.bat`) do set "VCVARS=%%I"

if not defined VCVARS (
  echo [zpc] Unable to locate vcvarsall.bat for an x64 MSVC toolchain.
  exit /b 1
)

call "%VCVARS%" x64 >nul
if errorlevel 1 exit /b %errorlevel%

set "VSROOT="
for %%I in ("%VCVARS%") do set "VSROOT=%%~dpI..\..\.."

set "VS_NINJA=%VSROOT%\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
if not exist "%VS_NINJA%" (
  echo [zpc] Unable to locate Visual Studio bundled Ninja at "%VS_NINJA%"
  exit /b 1
)

for %%I in ("%VS_NINJA%") do set "PATH=%%~dpI;%PATH%"
exit /b %errorlevel%