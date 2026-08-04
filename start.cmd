@echo off
rem Buduje, instaluje i uruchamia - wszystko jednym poleceniem.
rem Do codziennego uzytku sluzy skrot "Claude" na pulpicie; ten plik jest po to,
rem zeby po sklonowaniu repozytorium dojsc do dzialajacego programu w jednym kroku.

setlocal
set "ROOT=%~dp0"

echo [1/3] Kompilacja...
call "%ROOT%build.cmd"
if errorlevel 1 (
    echo.
    echo Kompilacja nie powiodla sie - instalacja pominieta.
    exit /b 1
)

echo.
echo [2/3] Instalacja...
call "%ROOT%install.cmd"
if errorlevel 1 (
    echo.
    echo Instalacja nie powiodla sie.
    exit /b 1
)

echo.
echo [3/3] Uruchamianie...
start "" "%LOCALAPPDATA%\KKr\ClaudeProfiles\claude-profiles.exe"

echo.
echo Gotowe. Na pulpicie jest skrot "Claude" - otwiera okno wyboru konta.
exit /b 0
