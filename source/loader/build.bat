@echo off
rem Build CwoLoader.dll (x64, static runtime, no extra DLL dependencies), CwoLaunch.exe, CwoSign.exe and the offline tests.
rem Needs Visual Studio's C++ tools (x64): run it from an "x64 Native Tools Command Prompt", or set CWO_VCVARS to the full
rem path of vcvars64.bat; otherwise Visual Studio is found with vswhere.
setlocal
if defined VCINSTALLDIR goto have
if not defined CWO_VCVARS goto findvs
call "%CWO_VCVARS%" >nul || exit /b 1
goto have
:findvs
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" goto novs
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -property installationPath`) do set "VSDIR=%%i"
if not defined VSDIR goto novs
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
goto have
:novs
echo Visual Studio C++ tools not found - run this from an x64 Native Tools Command Prompt, or set CWO_VCVARS.
exit /b 1
:have
cd /d "%~dp0"
if not exist build mkdir build
cl /nologo /O2 /MT /EHsc /W4 /LD src\cwo_loader.cpp /Fe:build\CwoLoader.dll /Fo:build\ /link /NOLOGO /MAP:build\CwoLoader.map || exit /b 1
cl /nologo /O2 /MT /W4 src\test_host.cpp /Fe:build\test_host.exe /Fo:build\ /link /NOLOGO || exit /b 1
cl /nologo /O2 /MT /W4 /EHsc src\cwo_launch.cpp /Fe:build\CwoLaunch.exe /Fo:build\ /link /NOLOGO /SUBSYSTEM:CONSOLE || exit /b 1
cl /nologo /O2 /MT /W4 /EHsc src\cwo_sign.cpp /Fe:build\CwoSign.exe /Fo:build\ /link /NOLOGO /SUBSYSTEM:CONSOLE || exit /b 1
cl /nologo /O2 /MT /W4 tests\pf_test.cpp /Fe:build\pf_test.exe /Fo:build\ /link /NOLOGO || exit /b 1
cl /nologo /O2 /MT /W4 tests\co_test.cpp /Fe:build\co_test.exe /Fo:build\ /link /NOLOGO || exit /b 1
cl /nologo /O2 /MT /W4 tests\of_test.cpp /Fe:build\of_test.exe /Fo:build\ /link /NOLOGO || exit /b 1
cl /nologo /O2 /MT /W4 tests\bs_test.cpp /Fe:build\bs_test.exe /Fo:build\ /link /NOLOGO || exit /b 1
echo built
