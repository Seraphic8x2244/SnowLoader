@echo off
REM Build script for SnowLoader
REM Requires Visual Studio 2022 and CMake

setlocal enabledelayedexpansion

REM Colors (Windows 10+ only, optional)
echo.
echo ========================================
echo SnowLoader Build Script
echo ========================================
echo.

REM Check if CMake is installed
where cmake >nul 2>nul
if errorlevel 1 (
    echo CMake not found. Please install CMake from https://cmake.org/download/
    echo.
    pause
    exit /b 1
)

REM Create build directory
if not exist "build" (
    echo Creating build directory...
    mkdir build
)

cd build

REM Configure
echo Configuring CMake...
cmake .. -G "Visual Studio 17 2022" -A x64
if errorlevel 1 (
    echo CMake configuration failed.
    pause
    exit /b 1
)

REM Build
echo.
echo Building Release configuration...
cmake --build . --config Release --parallel
if errorlevel 1 (
    echo Build failed.
    pause
    exit /b 1
)

REM Success
echo.
echo ========================================
echo Build completed successfully!
echo ========================================
echo.
echo Output location: %cd%\bin\Release\SnowLoader.asi
echo Runtime folder:  %cd%\..\assets\SnowLoader
echo.
pause
