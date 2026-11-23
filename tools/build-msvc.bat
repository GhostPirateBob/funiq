@echo off
rem Build bin\funiq-msvc.exe with the newest Visual Studio (or Build Tools)
rem that has the x64 C++ tools: /O2, static CRT (/MT). Run from anywhere;
rem used by `make build-cpp-msvc`. The release build stays MinGW (makefile).
setlocal
set "ROOT=%~dp0.."
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (echo build-msvc: vswhere.exe not found; install Visual Studio Build Tools & exit /b 1)
set "VS="
for /f "usebackq delims=" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
if not defined VS (echo build-msvc: no Visual Studio with the x64 C++ tools found & exit /b 1)
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
if not exist "%ROOT%\bin\msvc-obj" mkdir "%ROOT%\bin\msvc-obj"
rem C4267/C4305: size_t and double narrowing that the code does on purpose;
rem _CRT_SECURE_NO_WARNINGS: fopen/getenv/strerror are the portable calls.
cl /nologo /std:c++17 /EHsc /MT /O2 /W3 /wd4267 /wd4305 /D_CRT_SECURE_NO_WARNINGS ^
   /I"%ROOT%\src\cpp" /I"%ROOT%\lib" "%ROOT%\src\cpp\funiq.cpp" ^
   /Fo"%ROOT%\bin\msvc-obj\\" /Fe"%ROOT%\bin\funiq-msvc.exe" || exit /b 1
echo Built bin\funiq-msvc.exe
endlocal
