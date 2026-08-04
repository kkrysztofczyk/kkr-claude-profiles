@echo off
setlocal enabledelayedexpansion

set "ROOT=%~dp0"
set "OUT=%ROOT%build"

rem --- lokalizacja toolchaina MSVC -------------------------------------------
rem Nie uzywamy vswhere: jego katalog "Program Files (x86)" zawiera nawiasy, ktore
rem cmd traktuje jak koniec bloku niezaleznie od cytowania. Zamiast tego sondujemy
rem znane lokalizacje, a sciezki z nawiasami podstawiamy przez !zmienne! - opoznione
rem rozwijanie nastepuje juz po sparsowaniu bloku, wiec nawiasy sa nieszkodliwe.

set "PFX64=%ProgramFiles%"
set "PFX86=%ProgramFiles(x86)%"
set "VCVARS="

for %%r in ("!PFX64!" "!PFX86!") do (
    for %%v in (18 2026 2022 2019 2017) do (
        for %%e in (Community Professional Enterprise BuildTools Preview) do (
            if not defined VCVARS (
                set "CANDIDATE=%%~r\Microsoft Visual Studio\%%v\%%e\VC\Auxiliary\Build\vcvars64.bat"
                if exist "!CANDIDATE!" set "VCVARS=!CANDIDATE!"
            )
        )
    )
)

if not defined VCVARS (
    echo BLAD: nie znaleziono vcvars64.bat.
    echo Zainstaluj workload "Desktop development with C++" w Visual Studio.
    exit /b 1
)

echo Toolchain: !VCVARS!
call "!VCVARS!" >nul
if errorlevel 1 (
    echo BLAD: vcvars64.bat nie powiodl sie.
    exit /b 1
)

rem --- kompilacja -------------------------------------------------------------
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"

rc /nologo /i "%ROOT%res" /fo launcher.res "%ROOT%res\launcher.rc"
if errorlevel 1 goto :fail

cl /nologo /std:c++17 /W4 /WX /permissive- /utf-8 /EHsc /O2 /MT /DUNICODE /D_UNICODE ^
   "%ROOT%src\main.cpp" "%ROOT%src\chooser.cpp" launcher.res ^
   /Fe:claude-profiles.exe ^
   /link /SUBSYSTEM:WINDOWS ole32.lib shell32.lib user32.lib advapi32.lib comctl32.lib ^
   gdiplus.lib dwmapi.lib gdi32.lib
if errorlevel 1 goto :fail

popd
echo.
echo OK: %OUT%\claude-profiles.exe
exit /b 0

:fail
popd
echo.
echo BLAD kompilacji.
exit /b 1
