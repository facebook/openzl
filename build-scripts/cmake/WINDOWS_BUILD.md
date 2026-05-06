# OpenZL Windows Build Guide

## The Problem

OpenZL uses C11 features (like designated initializers: `{.field = value}`) that cause **C2099 errors** in MSVC because MSVC has incomplete C11 support.

## The Solution

Use a different compiler that supports C11 properly.

## Your Options

### Option 1: clang-cl (Recommended)

```bash
cmake -S . -B build -T ClangCL
cmake --build build --config Release
```

**Pros:** Full C11 support, works with Visual Studio
**Cons:** Requires clang-cl installation

### Option 2: MinGW-w64

```bash
cmake -S . -B build -G "MinGW Makefiles"
cmake --build build --config Release
```

**Pros:** Full C11 support, GNU toolchain
**Cons:** Different runtime, less Windows integration

### Option 3: MSVC (Not Recommended)

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

**Pros:** Easy setup if you have Visual Studio
**Cons:** **Will likely fail with C2099 errors**

## Quick Start

1. **Have Visual Studio?** → Install clang-cl component, use Option 1
2. **Prefer GNU tools?** → Install MinGW-w64, use Option 2
3. **Want to try MSVC anyway?** → Use Option 3 (expect errors)

## Installation

### clang-cl

- Via Visual Studio Installer: Add "Clang compiler for Windows" component
- Via LLVM: Download from <https://releases.llvm.org/>

### MinGW-w64

- Via MSYS2: `pacman -S mingw-w64-x86_64-gcc`
  - For `zli` and test targets: also install `mingw-w64-x86_64-cmake`
- Via w64devkit: Download from <https://github.com/skeeto/w64devkit>

## Examples

### Basic Build

```bash
git clone <repository>
cd openzl
cmake -S . -B build -T ClangCL
cmake --build build --config Release
```

### Debug Build

```bash
cmake -S . -B build -T ClangCL -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug
```

## Troubleshooting

### Common Issues

#### **clang-cl not found**: Ensure Visual Studio with C++ workload is installed

```powershell
# Check if clang-cl is available
where clang-cl
```

**Solution:** Install via Visual Studio Installer or standalone LLVM.

#### **Toolset not recognized**: Verify CMake version supports ClangCL toolset

#### **C2099 errors persist**: Check CMakeCache.txt to verify clang-cl is actually being used

**Solution:** You're using MSVC. Switch to clang-cl:

```bash
rm -rf cmake-build
cmake -S . -B cmake-build -T ClangCL
cmake --build cmake-build --config Release
```

### Debugging

1. Check CMakeCache.txt for compiler paths and IDs
2. Use the diagnostic scripts to verify detection logic
3. Review CI logs for detailed compiler information

### Need Help Choosing?

#### Step 1: Get Recommendations

```powershell
# PowerShell - Get compiler recommendations and installation instructions
./build-scripts/cmake/detect_windows_compiler.ps1

# Command Prompt - Get compiler recommendations and installation instructions
./build-scripts/cmake/detect_windows_compiler.bat
```

#### Step 2: Run Complete Setup Helper

```powershell
# PowerShell - Comprehensive setup: detect, recommend, and test
./build-scripts/cmake/detect_windows_compiler.ps1

# For CI/automation (non-interactive)
./build-scripts/cmake/detect_windows_compiler.ps1 -CI

# Only test configurations (skip detection phase)
./build-scripts/cmake/detect_windows_compiler.ps1 -TestOnly
```

This script will:

- Detect available compilers
- Provide recommendations and installation instructions
- Test your configuration to verify it works
- Report final results and next steps
- Return appropriate exit codes for CI/automation
