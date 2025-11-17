"""
AUTO-GENERATED FILE - DO NOT EDIT MANUALLY

Generated from: src/openzl/compress/graphs/sddl2/sddl2_opcodes.def
Generated at: 2025-11-16 22:46:06 UTC
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
    "CALL": 0x000B,
    "SEGMENT": 0x000C,
}

# Instruction definitions
# Format: "mnemonic": (family_name, opcode, [param_types])
INSTRUCTIONS = {
    # CONTROL family (0x0005)
    "halt": ("CONTROL", 0x0001, []),
    "expect_true": ("CONTROL", 0x0002, []),

    # PUSH family (0x0001)
    "push.zero": ("PUSH", 0x0001, []),
    "push.u32": ("PUSH", 0x0002, ["u32"]),
    "push.i32": ("PUSH", 0x0003, ["i32"]),
    "push.i64": ("PUSH", 0x0004, ["i64"]),
    "push.tag": ("PUSH", 0x0005, ["u32"]),
    "push.current_pos": ("PUSH", 0x0080, []),
    "push.remaining": ("PUSH", 0x0081, []),
    "push.stack_depth": ("PUSH", 0x0082, []),
    "push.type.bytes": ("PUSH", 0x0100, []),
    "push.type.u8": ("PUSH", 0x0110, []),
    "push.type.i8": ("PUSH", 0x0111, []),
    "push.type.u16le": ("PUSH", 0x0112, []),
    "push.type.u16be": ("PUSH", 0x0113, []),
    "push.type.i16le": ("PUSH", 0x0114, []),
    "push.type.i16be": ("PUSH", 0x0115, []),
    "push.type.u32le": ("PUSH", 0x0116, []),
    "push.type.u32be": ("PUSH", 0x0117, []),
    "push.type.i32le": ("PUSH", 0x0118, []),
    "push.type.i32be": ("PUSH", 0x0119, []),
    "push.type.u64le": ("PUSH", 0x011A, []),
    "push.type.u64be": ("PUSH", 0x011B, []),
    "push.type.i64le": ("PUSH", 0x011C, []),
    "push.type.i64be": ("PUSH", 0x011D, []),
    "push.type.f8": ("PUSH", 0x0130, []),
    "push.type.f16le": ("PUSH", 0x0131, []),
    "push.type.f16be": ("PUSH", 0x0132, []),
    "push.type.bf16le": ("PUSH", 0x0133, []),
    "push.type.bf16be": ("PUSH", 0x0134, []),
    "push.type.f32le": ("PUSH", 0x0135, []),
    "push.type.f32be": ("PUSH", 0x0136, []),
    "push.type.f64le": ("PUSH", 0x0137, []),
    "push.type.f64be": ("PUSH", 0x0138, []),

    # STACK family (0x0007)
    "stack.dup": ("STACK", 0x0001, []),
    "stack.over": ("STACK", 0x0002, []),
    "stack.drop": ("STACK", 0x0003, []),
    "stack.swap": ("STACK", 0x0004, []),
    "stack.rot": ("STACK", 0x0005, []),
    "stack.drop_if": ("STACK", 0x0010, []),

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

    # LOGIC family (0x0004)
    "logic.and": ("LOGIC", 0x0001, []),
    "logic.or": ("LOGIC", 0x0002, []),
    "logic.xor": ("LOGIC", 0x0003, []),
    "logic.not": ("LOGIC", 0x0004, []),

    # LOAD family (0x0006)
    "load.u8": ("LOAD", 0x0001, []),
    "load.i8": ("LOAD", 0x0002, []),
    "load.u16le": ("LOAD", 0x0010, []),
    "load.i16le": ("LOAD", 0x0011, []),
    "load.u32le": ("LOAD", 0x0020, []),
    "load.i32le": ("LOAD", 0x0021, []),
    "load.i64le": ("LOAD", 0x0030, []),
    "load.u16be": ("LOAD", 0x0110, []),
    "load.i16be": ("LOAD", 0x0111, []),
    "load.u32be": ("LOAD", 0x0120, []),
    "load.i32be": ("LOAD", 0x0121, []),
    "load.i64be": ("LOAD", 0x0130, []),

    # TYPE family (0x0008)
    "type.fixed_array": ("TYPE", 0x0001, []),
    "type.structure": ("TYPE", 0x0002, []),
    "type.sizeof": ("TYPE", 0x0010, []),

    # SEGMENT family (0x000C)
    "segment.create_unspecified": ("SEGMENT", 0x0001, []),
    "segment.create_tagged": ("SEGMENT", 0x0002, []),

}
