:: Copyright (c) Meta Platforms, Inc. and affiliates.

@echo off
setlocal enabledelayedexpansion

:: Windows Compiler Detection and Recommendation Script for OpenZL
:: Batch file version for basic Windows compatibility

echo OpenZL Windows Compiler Detection and Setup Helper
echo =================================================
echo.

:: Check for available compilers
echo Checking available compilers...
echo.

set CLANG_CL_AVAILABLE=false
set MSVC_AVAILABLE=false
set MINGW_AVAILABLE=false

:: Check for clang-cl
clang-cl --version >nul 2>&1
if !errorlevel! == 0 (
    set CLANG_CL_AVAILABLE=true
    echo ^✓ clang-cl found
) else (
    echo ^✗ clang-cl not found
)

:: Check for MSVC (cl.exe)
cl >nul 2>&1
if !errorlevel! == 0 (
    set MSVC_AVAILABLE=true
    echo ^✓ MSVC found
) else (
    echo ^✗ MSVC ^(cl.exe^) not found
)

:: Check for MinGW GCC
gcc --version 2>nul | find "mingw" >nul
if !errorlevel! == 0 (
    set MINGW_AVAILABLE=true
    echo ^✓ MinGW GCC found
) else (
    echo ^✗ MinGW GCC not found
)

echo.
echo Compiler Recommendations:
echo ========================

if "!CLANG_CL_AVAILABLE!" == "true" (
    echo ^🎯 RECOMMENDED: Use clang-cl
    echo    - Excellent C11 support
    echo    - Compatible with Visual Studio
    echo    - No C2099 errors expected
    echo.
    echo    Build commands:
    echo    cmake -S . -B build -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl
    echo    cmake --build build --config Release
    echo.
) else if "!MINGW_AVAILABLE!" == "true" (
    echo ^🎯 RECOMMENDED: Use MinGW-w64
    echo    - Full C11 support
    echo    - GNU toolchain compatibility
    echo    - No C2099 errors expected
    echo.
    echo    Build commands:
    echo    cmake -S . -B build -G "MinGW Makefiles"
    echo    cmake --build build --config Release
    echo.
) else if "!MSVC_AVAILABLE!" == "true" (
    echo ^⚠️  CAUTION: MSVC has limited C11 support
    echo    - May produce C2099 errors
    echo    - Consider installing clang-cl
    echo.
    echo    Build commands ^(may fail^):
    echo    cmake -S . -B build -G "Visual Studio 16 2019" -A x64
    echo    cmake --build build --config Release
    echo.
) else (
    echo ^❌ No suitable compiler found!
    echo.
    echo Installation recommendations:
    echo 1. Install Visual Studio with clang-cl component
    echo 2. Or install standalone LLVM with clang-cl
    echo 3. Or install MinGW-w64
    echo.
)

:: Provide installation instructions
echo Installation Instructions:
echo =========================
echo.

if "!CLANG_CL_AVAILABLE!" == "false" (
    echo To install clang-cl:
    echo Option 1 - Via Visual Studio:
    echo   1. Open Visual Studio Installer
    echo   2. Modify your installation
    echo   3. Add 'Clang compiler for Windows' component
    echo.
    echo Option 2 - Via LLVM:
    echo   1. Download LLVM from https://releases.llvm.org/
    echo   2. Install LLVM
    echo   3. Add LLVM/bin to PATH
    echo.
)

if "!MINGW_AVAILABLE!" == "false" (
    echo To install MinGW-w64:
    echo Option 1 - Via MSYS2:
    echo   1. Install MSYS2
    echo   2. pacman -S mingw-w64-x86_64-gcc
    echo   3. pacman -S mingw-w64-x86_64-cmake
    echo.
    echo Option 2 - Via w64devkit:
    echo   1. Download from https://github.com/skeeto/w64devkit
    echo   2. Extract and add to PATH
    echo.
)

echo For detailed build instructions, see build-scripts/cmake/WINDOWS_BUILD.md
echo.
echo Happy building! ^🚀

pause
