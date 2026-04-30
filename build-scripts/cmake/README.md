# OpenZL CMake Build System

This directory contains the CMake build system configuration for OpenZL, including Windows-specific compiler support.

## Files Overview

### Core CMake Files

- `OpenZLCompilerUnix.cmake` - Unix/Linux compiler configuration
- `OpenZLCompilerMSVC.cmake` - MSVC and clang-cl compiler configuration
- `OpenZLConfigChecks.cmake` - Configuration checks and feature detection
- `OpenZLFunctions.cmake` - Custom CMake functions
- `OpenZLModeFlags.cmake` - Build mode flags
- `openzl-config.cmake.in` - CMake package configuration template
- `openzl-deps.cmake` - Dependency management
- `zl_config.h.cmake` - Configuration header template

### Windows Support

- `detect_windows_compiler.ps1` - PowerShell compiler detection and testing script
- `detect_windows_compiler.bat` - Batch file compiler detection script

### Documentation

- `README.md` - this file
- `WINDOWS_BUILD.md` - Windows build instructions, strategy, and options

## Quick Reference

For Windows builds, see `WINDOWS_BUILD.md` for detailed instructions.

**Recommended approach:**

```bash
cmake -S . -B build -T ClangCL
cmake --build build --config Release
```

**Compiler detection:**

```bash
./build-scripts/cmake/detect_windows_compiler.ps1
```
