#!/bin/bash
# Copyright (c) Meta Platforms, Inc. and affiliates.
#
# Check Python file formatting using black.
# Reports file and line numbers when formatting issues are detected.
#
# Usage: ./scripts/check_python_format.sh
#
# Exit codes:
#   0 - All files are correctly formatted
#   1 - Formatting issues detected

set -euo pipefail

# Check if black is installed
if ! command -v black &> /dev/null; then
    echo "Error: 'black' is not installed."
    echo "Install it with: pip install black"
    exit 1
fi

# Explicitly list source directories containing Python files.
# This avoids scanning large build/deps directories.
PYTHON_DIRS=(
    benchmark
    cli
    custom_parsers
    examples
    py
    scripts
    src
    tests
    tools
)

# Filter to only existing directories
EXISTING_DIRS=()
for dir in "${PYTHON_DIRS[@]}"; do
    if [[ -d "$dir" ]]; then
        EXISTING_DIRS+=("$dir")
    fi
done

if [[ ${#EXISTING_DIRS[@]} -eq 0 ]]; then
    echo "No Python source directories found."
    exit 0
fi

echo "Checking Python file formatting with black..."
echo "Directories: ${EXISTING_DIRS[*]}"
echo ""

# Run black in check mode with diff output
set +e
BLACK_OUTPUT=$(black --check --diff --color "${EXISTING_DIRS[@]}" 2>&1)
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
        echo "   To fix all issues: Run 'black ${EXISTING_DIRS[*]}' from the repository root."
    elif [[ -n "$line" ]]; then
        echo "$line"
    fi
done <<< "$BLACK_OUTPUT"

echo ""
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "Python formatting check failed!"
echo "Run 'black ${EXISTING_DIRS[*]}' locally to auto-format all Python files."
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
exit 1
