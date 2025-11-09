"""
AUTO-GENERATED FILE - DO NOT EDIT MANUALLY

Generated from: src/openzl/compress/graphs/sddlv2/sddl2_opcodes.def
Generated at: 2025-11-09 04:24:28 UTC
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
    "SEGMENT": 0x000C,
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
    "push.tag": ("PUSH", 0x0005, ["u32"]),

    # STACK family (0x0007)
    "stack.dup": ("STACK", 0x0001, []),
    "stack.drop": ("STACK", 0x0002, []),
    "stack.swap": ("STACK", 0x0003, []),
    "stack.over": ("STACK", 0x0004, []),
    "stack.rot": ("STACK", 0x0005, []),

    # MATH family (0x0002)
    "math.add": ("MATH", 0x0001, []),
    "math.sub": ("MATH", 0x0002, []),
    "math.mul": ("MATH", 0x0003, []),
    "math.div": ("MATH", 0x0004, []),
    "math.mod": ("MATH", 0x0005, []),
    "math.abs": ("MATH", 0x0006, []),
    "math.neg": ("MATH", 0x0007, []),

    # CMP family (0x0003)
    "cmp.eq": ("CMP", 0x0001, []),
    "cmp.ne": ("CMP", 0x0002, []),
    "cmp.lt": ("CMP", 0x0003, []),
    "cmp.le": ("CMP", 0x0004, []),
    "cmp.gt": ("CMP", 0x0005, []),
    "cmp.ge": ("CMP", 0x0006, []),

    # TYPE family (0x0008)
    "type.const": ("TYPE", 0x0001, ["u32", "u32"]),

    # SEGMENT family (0x000C)
    "segment.create_unspecified": ("SEGMENT", 0x0001, []),
    "segment.create_tagged": ("SEGMENT", 0x0002, []),

}
