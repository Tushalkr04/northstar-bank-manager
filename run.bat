@echo off
setlocal EnableExtensions EnableDelayedExpansion
cd /d "%~dp0"

powershell.exe -NoProfile -Command "try { $r = Invoke-WebRequest -UseBasicParsing -Uri 'http://127.0.0.1:8080/api/accounts' -TimeoutSec 2; if ($r.StatusCode -eq 200 -and $r.Content.TrimStart().StartsWith('[')) { exit 0 } } catch {}; exit 1" >nul 2>nul
if not errorlevel 1 (
    echo Bank Manager is already running. Opening the dashboard...
    start "" "http://127.0.0.1:8080"
    exit /b 0
)

set "BUILD_DIR=%~dp0build"
set "COMPILER="
for /f "delims=" %%G in ('where g++.exe 2^>nul') do (
    set "COMPILER=%%G"
    goto :compiler_found
)

for %%G in ("%USERPROFILE%\scoop\apps\msys2\current\ucrt64\bin\g++.exe" "C:\msys64\ucrt64\bin\g++.exe" "C:\msys64\mingw64\bin\g++.exe" "C:\MinGW\bin\g++.exe") do (
    if exist "%%~G" (
        set "COMPILER=%%~G"
        goto :compiler_found
    )
)

:compiler_found
if defined COMPILER (
    for %%G in ("%COMPILER%") do set "TOOLCHAIN_BIN=%%~dpG"
    set "PATH=!TOOLCHAIN_BIN!;!PATH!"
)

if defined COMPILER (
    echo Building Bank Manager with %COMPILER%...
    if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"
    "%COMPILER%" -std=c++17 -O2 -Wall -Wextra -Wpedantic "%~dp0src\main.cpp" -lws2_32 -o "%BUILD_DIR%\bank_manager.exe"
    if errorlevel 1 goto :build_failed
    if not exist "%BUILD_DIR%\public" mkdir "%BUILD_DIR%\public"
    xcopy "%~dp0public\*" "%BUILD_DIR%\public\" /E /I /Y >nul
    if errorlevel 1 goto :build_failed
) else (
    where cmake.exe >nul 2>nul
    if errorlevel 1 (
        echo Could not find CMake or the g++ C++ compiler.
        echo Install CMake with a C++ build toolchain, or install MinGW-w64.
        pause
        exit /b 1
    )
    echo Building Bank Manager with CMake...
    cmake -S "%~dp0." -B "%BUILD_DIR%"
    if errorlevel 1 goto :build_failed
    cmake --build "%BUILD_DIR%" --config Release
    if errorlevel 1 goto :build_failed
)

set "SERVER=%BUILD_DIR%\bank_manager.exe"
if not exist "%SERVER%" set "SERVER=%BUILD_DIR%\Release\bank_manager.exe"
if not exist "%SERVER%" (
    echo The build succeeded, but bank_manager.exe was not found in the build folder.
    pause
    exit /b 1
)

echo Starting the local banking service...
start "Northstar Bank Manager" /min "%SERVER%"

powershell.exe -NoProfile -Command "$deadline = (Get-Date).AddSeconds(30); do { try { $r = Invoke-WebRequest -UseBasicParsing -Uri 'http://127.0.0.1:8080/api/accounts' -TimeoutSec 1; if ($r.StatusCode -eq 200 -and $r.Content.TrimStart().StartsWith('[')) { exit 0 } } catch {}; Start-Sleep -Milliseconds 500 } while ((Get-Date) -lt $deadline); exit 1"
if errorlevel 1 (
    echo Bank Manager did not start. Port 8080 may be in use.
    echo Close the other app using port 8080, then run this file again.
    pause
    exit /b 1
)

echo Bank Manager is ready. Opening the dashboard...
start "" "http://127.0.0.1:8080"
echo Leave the Bank Manager server window open while you use the app.
exit /b 0

:build_failed
echo Build failed. Check that C++17 and the Windows Winsock libraries are available.
pause
exit /b 1
