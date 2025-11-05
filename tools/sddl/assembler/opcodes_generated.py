"""
AUTO-GENERATED FILE - DO NOT EDIT MANUALLY

Generated from: openzl_opcodes.h
Generated at: 2025-11-05 07:30:48 UTC
Generator: generate_opcodes.py

To regenerate: python3 generate_opcodes.py
"""

# Family identifiers
FAMILIES = {
    "PUSH": 0x0001,
    "MATH": 0x0002,
    "CMP": 0x0003,
    "LOGIC": 0x0004,
    "CONTROL": 0x0005,
    "LOAD": 0x0006,
    "STACK": 0x0007,
    "TYPE": 0x0008,
    "VAR": 0x0009,
    "EXPECT": 0x000A,
    "CALL": 0x000B,
}

# Instruction definitions
# Format: "mnemonic": (family_name, opcode, [param_types])
INSTRUCTIONS = {
    # CONTROL family (0x0005)
    "halt": ("CONTROL", 0x0001, []),

    # PUSH family (0x0001)
    "push.zero": ("PUSH", 0x0001, []),
    "push.u32": ("PUSH", 0x0002, ["u32"]),
    "push.i32": ("PUSH", 0x0003, ["i32"]),
    "push.i64": ("PUSH", 0x0004, ["i64"]),

    # STACK family (0x0007)
    "stack.dup": ("STACK", 0x0001, []),
    "stack.drop": ("STACK", 0x0002, []),
    "stack.swap": ("STACK", 0x0003, []),
    "stack.over": ("STACK", 0x0004, []),
    "stack.rot": ("STACK", 0x0005, []),

}
