#!/bin/bash
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Check or fix Python file formatting using black.
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

# Check if black is installed
if ! command -v black &> /dev/null; then
    echo "Error: 'black' is not installed."
    echo "Install it with: pip install black"
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
# Pass files to black instead of directories - avoids black's slow directory traversal
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
    echo "Fixing Python file formatting with black..."
    echo "Found ${#PYTHON_FILES[@]} Python files"
    echo ""
    black "${PYTHON_FILES[@]}"
    echo ""
    echo "✓ Python formatting complete."
    exit 0
fi

# Check mode
echo "Checking Python file formatting with black..."
echo "Found ${#PYTHON_FILES[@]} Python files"
echo ""

# Run black in check mode with diff output
set +e
BLACK_OUTPUT=$(black --check --diff --color "${PYTHON_FILES[@]}" 2>&1)
BLACK_EXIT_CODE=$?
set -e

if [[ $BLACK_EXIT_CODE -eq 0 ]]; then
    echo "✓ All Python files are correctly formatted."
    exit 0
fi

# Parse and display the diff output with enhanced information
CURRENT_FILE=""
while IFS= read -r line; do
    # Detect file being processed from diff header
    if [[ "$line" =~ ^"--- " ]]; then
        CURRENT_FILE=$(echo "$line" | sed 's/^--- //' | sed 's/[[:space:]]*$//')
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
    elif [[ "$line" =~ "would reformat" ]]; then
        echo ""
        echo "❌ $line"
        echo "   Explanation: This file does not match black's formatting standards."
        echo "   To fix: Run 'black <file>' locally to auto-format."
    elif [[ "$line" =~ "file would be left unchanged" ]] || [[ "$line" =~ "files would be left unchanged" ]]; then
        echo "✓ $line"
    elif [[ "$line" =~ "Oh no!" ]]; then
        echo ""
        echo "❌ $line"
        echo "   Explanation: Some Python files need reformatting."
        echo "   To fix all issues: Run 'make fix-python-format'"
    elif [[ -n "$line" ]]; then
        echo "$line"
    fi
done <<< "$BLACK_OUTPUT"

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Python formatting check failed!"
echo "Run 'make fix-python-format' to auto-format all Python files."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
exit 1
