#!/bin/bash
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Check or fix Python file formatting using ruff.
# Reports file and line numbers when formatting issues are detected.
#
# Usage: ./scripts/check_python_format.sh [--fix]
#   --fix: Apply formatting fixes instead of just checking
#
# Exit codes:
#   0 - All files are correctly formatted (or fixed successfully)
#   1 - Formatting issues detected (check mode only)

set -euo pipefail

# Parse arguments
FIX_MODE=false
if [[ "${1:-}" == "--fix" ]]; then
    FIX_MODE=true
fi

# Check if ruff is installed
if ! command -v ruff &> /dev/null; then
    echo "Error: 'ruff' is not installed."
    echo "Install it with: pip install ruff"
    exit 1
fi

# Directories to exclude (build artifacts, dependencies, caches)
EXCLUDE_DIRS=(deps build cachedObjs cmakebuild .git __pycache__ .eggs build-py node_modules .cache .claude .vscode)

# Build the find prune expression
PRUNE_EXPR=""
for dir in "${EXCLUDE_DIRS[@]}"; do
    if [[ -n "$PRUNE_EXPR" ]]; then
        PRUNE_EXPR="$PRUNE_EXPR -o"
    fi
    PRUNE_EXPR="$PRUNE_EXPR -name $dir"
done

# Find all Python files directly (skip excluded directories with -prune)
# Pass files to ruff instead of directories - avoids slow directory traversal
PYTHON_FILES=()
while IFS= read -r file; do
    [[ -n "$file" ]] && PYTHON_FILES+=("$file")
done < <(
    find . \( $PRUNE_EXPR \) -prune -o -name "*.py" -type f -print
)

if [[ ${#PYTHON_FILES[@]} -eq 0 ]]; then
    echo "No Python files found."
    exit 0
fi

if [[ "$FIX_MODE" == true ]]; then
    echo "Fixing Python file formatting with ruff..."
    echo "Found ${#PYTHON_FILES[@]} Python files"
    echo ""
    ruff format "${PYTHON_FILES[@]}"
    echo ""
    echo "✓ Python formatting complete."
    exit 0
fi

# Check mode
echo "Checking Python file formatting with ruff..."
echo "Found ${#PYTHON_FILES[@]} Python files"
echo ""

# Run ruff format in check mode with diff output
set +e
RUFF_OUTPUT=$(ruff format --check --diff "${PYTHON_FILES[@]}" 2>&1)
RUFF_EXIT_CODE=$?
set -e

if [[ $RUFF_EXIT_CODE -eq 0 ]]; then
    echo "✓ All Python files are correctly formatted."
    exit 0
fi

# Parse and display the diff output with enhanced information
while IFS= read -r line; do
    # Detect file being processed from diff header
    if [[ "$line" =~ ^"--- " ]]; then
        echo "$line"
    elif [[ "$line" =~ ^"+++ " ]]; then
        echo "$line"
    elif [[ "$line" =~ ^"@@" ]]; then
        # Extract line numbers from diff hunk header: @@ -start,count +start,count @@
        echo "$line"
        if [[ "$line" =~ @@\ -([0-9]+) ]]; then
            LINE_NUM="${BASH_REMATCH[1]}"
            echo "  → Line $LINE_NUM: Formatting issue detected"
        fi
    elif [[ "$line" =~ ^[-+] ]] && [[ ! "$line" =~ ^"---" ]] && [[ ! "$line" =~ ^"+++" ]]; then
        # Show actual diff lines (additions/removals)
        echo "$line"
    elif [[ "$line" =~ "would be reformatted" ]]; then
        echo ""
        echo "❌ $line"
        echo "   Explanation: This file does not match ruff's formatting standards."
        echo "   To fix: Run 'ruff format <file>' locally to auto-format."
    elif [[ "$line" =~ "file already formatted" ]] || [[ "$line" =~ "files already formatted" ]]; then
        echo "✓ $line"
    elif [[ -n "$line" ]]; then
        echo "$line"
    fi
done <<< "$RUFF_OUTPUT"

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Python formatting check failed!"
echo "Run 'make fix-python-format' to auto-format all Python files."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
exit 1
