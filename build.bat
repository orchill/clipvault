@echo off
rem One-shot build script: prefers MSVC (via vswhere), falls back to MinGW g++.
setlocal
cd /d "%~dp0"

rem --- generate icon if missing ---
if not exist "resources\app.ico" (
  echo Generating icon...
  where g++ >nul 2>nul && (g++ -O2 -std=c++20 tools\gen_icon.cpp -o tools\gen_icon.exe && tools\gen_icon.exe resources\app.ico)
  if not exist "resources\app.ico" (
    echo ERROR: could not generate resources\app.ico
    exit /b 1
  )
)

rem --- MSVC path ---
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist "%VSWHERE%" (
  for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VS=%%i"
)
if defined VS (
  if exist "%VS%\VC\Auxiliary\Build\vcvars64.bat" (
    echo Building with MSVC...
    call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul
    if not exist build mkdir build
    rc /nologo /fo build\app.res resources\app.rc || exit /b 1
    cl /nologo /O2 /EHsc /W4 /DUNICODE /D_UNICODE /DWIN32_LEAN_AND_MEAN /DNOMINMAX /D_CRT_SECURE_NO_WARNINGS ^
       src\*.cpp build\app.res /link /SUBSYSTEM:WINDOWS /OUT:ClipVault.exe ^
       gdi32.lib user32.lib shell32.lib advapi32.lib comctl32.lib dwmapi.lib uxtheme.lib gdiplus.lib
    exit /b %errorlevel%
  )
)

rem --- MinGW path ---
where g++ >nul 2>nul || (echo ERROR: no MSVC and no g++ found & exit /b 1)
where windres >nul 2>nul || (echo ERROR: windres not found in PATH & exit /b 1)
echo Building with MinGW g++...
if not exist build mkdir build
windres resources\app.rc -O coff -o build\app_res.o || exit /b 1
rem Shadow the toolchain's bare default manifest with ours (PerMonitorV2 + comctl6)
windres resources\manifest_only.rc -O coff -o build\default-manifest.o || exit /b 1
g++ -std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter -DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS -municode -mwindows -Bbuild ^
  src\*.cpp build\app_res.o ^
  -o ClipVault.exe ^
  -lgdiplus -ldwmapi -lcomctl32 -luxtheme -lshell32 -ladvapi32 -luuid -lole32 -static -static-libgcc -static-libstdc++ -s || exit /b 1
echo Built ClipVault.exe
endlocal
