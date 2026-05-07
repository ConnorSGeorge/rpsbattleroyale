@echo off
setlocal EnableExtensions

for %%I in ("%~dp0.") do set "SCRIPT_DIR=%%~fI"
cd /d "%SCRIPT_DIR%" || exit /b 1

rem This script lives in the `rps` folder; use that folder as the CMake source.
set "SRC_DIR=%SCRIPT_DIR%"
set "BUILD_DIR=%SRC_DIR%\build"
set "BUILD_TYPE=Release"
set "GENERATOR=MinGW Makefiles"
set "CC=C:/mingw64/bin/gcc.exe"
set "CXX=C:/mingw64/bin/g++.exe"
set "GIT_SSL_NO_VERIFY=true"

if "%~1"=="clean" (
    echo Cleaning build directory...
    if exist "%BUILD_DIR%" rd /s /q "%BUILD_DIR%"
    exit /b 0
)

if not "%~1"=="" (
    echo Invalid parameter: %~1
    echo Usage: build.bat [clean]
    exit /b 1
)

mkdir "%BUILD_DIR%" 2>nul

echo Running CMake configuration...
cmake -S "%SRC_DIR%" -B "%BUILD_DIR%" -G "%GENERATOR%" -DCMAKE_C_COMPILER="%CC%" -DCMAKE_CXX_COMPILER="%CXX%" -DCMAKE_BUILD_TYPE="%BUILD_TYPE%" || exit /b 1

echo Building tnasm target...
cmake --build "%BUILD_DIR%" --config "%BUILD_TYPE%" --target tnasm || exit /b 1

echo Building RPS target...
cmake --build "%BUILD_DIR%" --config "%BUILD_TYPE%" --target RPS || exit /b 1

echo.
echo Build complete.

endlocal
