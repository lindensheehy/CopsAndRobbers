@echo off
rem Run from cmd. These paths match Valeriy's installed VS2022 + Windows SDK.
call "%ProgramFiles(x86)%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
set "WindowsSdkDir=%ProgramFiles(x86)%\Windows Kits\10\"
set "WindowsSDKVersion=10.0.26100.0\"
set "UniversalCRTSdkDir=%WindowsSdkDir%"
set "UCRTVersion=10.0.26100.0"
if not exist "%WindowsSdkDir%Include\%UCRTVersion%\ucrt\corecrt.h" (
    echo ERROR: Windows SDK 10.0.26100.0 not found. Update the version in this script.
    exit /b 1
)
set "INCLUDE=%WindowsSdkDir%Include\%UCRTVersion%\ucrt;%WindowsSdkDir%Include\%UCRTVersion%\shared;%WindowsSdkDir%Include\%UCRTVersion%\um;%INCLUDE%"
set "LIB=%WindowsSdkDir%Lib\%UCRTVersion%\ucrt\x64;%WindowsSdkDir%Lib\%UCRTVersion%\um\x64;%LIB%"
set "PATH=%SystemRoot%\System32;%SystemRoot%;%WindowsSdkDir%bin\%UCRTVersion%\x64;%PATH%"
if defined CUDA_PATH set "PATH=%CUDA_PATH%\bin;%PATH%"
python "%~dp0build.py"
exit /b %errorlevel%
