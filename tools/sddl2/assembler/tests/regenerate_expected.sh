#!/usr/bin/env bash
# Regenerate all .expected files from .asm files in tests/success/

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ASSEMBLER="$SCRIPT_DIR/../sddl2_assembler.py"

# Check if assembler exists
if [ ! -f "$ASSEMBLER" ]; then
    echo "Error: sddl2_assembler.py not found at $ASSEMBLER"
    exit 1
fi

if [ ! -d "$SUCCESS_DIR" ]; then
    echo "Error: success directory not found at $SUCCESS_DIR"
    exit 1
fi

echo "Regenerating .expected files from .asm files in $SUCCESS_DIR"
echo ""

count=0
for asm_file in "$SUCCESS_DIR"/*.asm; do
    if [ ! -f "$asm_file" ]; then
        continue
    fi

    base_name="${asm_file%.asm}"
    expected_file="${base_name}.expected"

    echo "Processing: $(basename "$asm_file")"

    # Use -c mode to avoid creating .bin files, read the .asm file content
    if python3 "$ASSEMBLER" -c "$(cat "$asm_file")" > "$expected_file.tmp" 2>&1; then
        mv "$expected_file.tmp" "$expected_file"
        echo "  ✓ Generated: $(basename "$expected_file")"
        ((count++))
    else
        echo "  ✗ Failed to assemble $(basename "$asm_file")"
        rm -f "$expected_file.tmp"
        exit 1
    fi
done

echo ""
echo "Successfully regenerated $count .expected files"
