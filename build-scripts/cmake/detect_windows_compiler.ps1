# Copyright (c) Meta Platforms, Inc. and affiliates.

# Windows Compiler Detection Script for OpenZL
# Simple, clear workflow: detect -> recommend -> test (optional)

param(
    [switch]$Help,
    [switch]$DetectOnly,
    [switch]$TestOnly,
    [switch]$CI
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Continue"

# Show help and exit
if ($Help) {
    Write-Host "OpenZL Windows Compiler Detection Script" -ForegroundColor Green
    Write-Host "=======================================" -ForegroundColor Green
    Write-Host ""
    Write-Host "USAGE:" -ForegroundColor Cyan
    Write-Host "  .\detect_windows_compiler.ps1           # Detect compilers and ask to test" -ForegroundColor White
    Write-Host "  .\detect_windows_compiler.ps1 -DetectOnly # Only detect compilers (no tests)" -ForegroundColor White
    Write-Host "  .\detect_windows_compiler.ps1 -TestOnly # Only test configurations" -ForegroundColor White
    Write-Host "  .\detect_windows_compiler.ps1 -Help     # Show this help" -ForegroundColor White
    Write-Host ""
    exit 0
}

# Detect CI/non-interactive mode
$IsCI = $CI -or $env:CI -eq "true" -or $env:GITHUB_ACTIONS -eq "true"

# Show header (except for TestOnly mode)
if (-not $TestOnly) {
    Write-Host "OpenZL Windows Compiler Detection" -ForegroundColor Green
    Write-Host "=================================" -ForegroundColor Green
    Write-Host ""
}

# Simple command existence check
function Test-Command {
    param([string]$Command)
    try {
        Get-Command $Command -ErrorAction Stop | Out-Null
        return $true
    } catch {
        return $false
    }
}

# Test one compiler configuration
function Test-CompilerConfig {
    param([string]$Name, [string]$Args)
    $BuildDir = "build_test_$Name"

    if (-not $IsCI) {
        Write-Host "Testing $Name..." -ForegroundColor Yellow
    }

    # Clean up
    if (Test-Path $BuildDir) {
        Remove-Item -Recurse -Force $BuildDir
    }

    # Configure
    try {
        Invoke-Expression "cmake -S . -B $BuildDir $Args" 2>&1 | Out-Null
        if ($LASTEXITCODE -eq 0) {
            # Test build system generation
            Invoke-Expression "cmake --build $BuildDir --target help" 2>&1 | Out-Null
            if ($LASTEXITCODE -eq 0) {
                Write-Host "[OK] ${Name}: PASSED" -ForegroundColor Green
                return $true
            }
        }
    } catch { }

    Write-Host "[X] ${Name}: FAILED" -ForegroundColor Red
    return $false
}

# STEP 1: DETECT COMPILERS
$ClangCL = Test-Command "clang-cl"
$MSVC = Test-Command "cl"
$MinGW = $false
if (Test-Command "gcc") {
    try {
        $GccOutput = & gcc --version 2>&1
        $MinGW = $GccOutput[0] -match "mingw"
    } catch { }
}

# STEP 2: SHOW DETECTION RESULTS (unless TestOnly)
if (-not $TestOnly) {
    Write-Host "Compiler Detection:" -ForegroundColor Cyan
    if ($ClangCL) { Write-Host "[OK] clang-cl found" -ForegroundColor Green } else { Write-Host "[X] clang-cl not found" -ForegroundColor Red }
    if ($MSVC) { Write-Host "[OK] MSVC found" -ForegroundColor Green } else { Write-Host "[X] MSVC not found" -ForegroundColor Red }
    if ($MinGW) { Write-Host "[OK] MinGW found" -ForegroundColor Green } else { Write-Host "[X] MinGW not found" -ForegroundColor Red }
    Write-Host ""

    # Show recommendation
    Write-Host "Recommendation:" -ForegroundColor Cyan
    if ($ClangCL) {
        Write-Host "Use clang-cl (best C11 support)" -ForegroundColor Green
        Write-Host "Build: cmake -S . -B build -T ClangCL && cmake --build build" -ForegroundColor White
    } elseif ($MinGW) {
        Write-Host "Use MinGW (good C11 support)" -ForegroundColor Green
        Write-Host "Build: cmake -S . -B build -G `"MinGW Makefiles`" && cmake --build build" -ForegroundColor White
    } elseif ($MSVC) {
        Write-Host "MSVC found but has limited C11 support - may fail" -ForegroundColor Yellow
        Write-Host "Build: cmake -S . -B build -G `"Visual Studio 17 2022`" -A x64 && cmake --build build" -ForegroundColor White
    } else {
        Write-Host "No compiler found. Install Visual Studio with clang-cl or MinGW-w64" -ForegroundColor Red
    }
    Write-Host ""

    # Show brief install help if no good compilers
    if (-not $ClangCL -and -not $MinGW) {
        Write-Host "Quick Install:" -ForegroundColor Cyan
        Write-Host "- clang-cl: Install Visual Studio with 'Clang compiler for Windows'" -ForegroundColor White
        Write-Host "- MinGW: Install MSYS2, then 'pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake'" -ForegroundColor White
        Write-Host ""
    }
}

# STEP 3: EXIT EARLY IF DETECT-ONLY
if ($DetectOnly) {
    exit 0
}

# STEP 4: EXIT EARLY IF NO COMPILERS AND NOT TESTING
if (-not ($ClangCL -or $MSVC -or $MinGW)) {
    if (-not $TestOnly) {
        Write-Host "No compilers available for testing" -ForegroundColor Red
    }
    exit 1
}

# STEP 5: ASK USER IF THEY WANT TO TEST (unless TestOnly or CI)
if (-not $TestOnly -and -not $IsCI) {
    $TestChoice = Read-Host "Test compiler configurations? (y/n)"
    if ($TestChoice -notmatch "^[yY]") {
        Write-Host "Skipping tests. Run with -TestOnly to test later." -ForegroundColor Yellow
        exit 0
    }
}

# STEP 6: RUN TESTS
Write-Host "Testing Configurations:" -ForegroundColor Cyan
$Results = @{}

# Test available compilers
if ($ClangCL) {
    $Results["clang-cl"] = Test-CompilerConfig "clang-cl" "-G `"Visual Studio 17 2022`" -A x64 -T ClangCL"
}
if ($MSVC) {
    $Results["msvc"] = Test-CompilerConfig "msvc" "-G `"Visual Studio 17 2022`" -A x64"
}
if ($MinGW) {
    $Results["mingw"] = Test-CompilerConfig "mingw" "-G `"MinGW Makefiles`""
}

# STEP 7: SHOW RESULTS AND EXIT
$PassedResults = $Results.Values | Where-Object { $_ }
$Passed = if ($PassedResults) { $PassedResults.Count } else { 0 }
$Total = $Results.Count

Write-Host ""
Write-Host "Results: $Passed/$Total passed" -ForegroundColor $(if ($Passed -eq $Total) { "Green" } else { "Yellow" })

if ($Passed -gt 0) {
    Write-Host "SUCCESS: Ready to build OpenZL!" -ForegroundColor Green
    exit 0
} else {
    Write-Host "FAILED: No working configurations" -ForegroundColor Red
    exit 1
}
