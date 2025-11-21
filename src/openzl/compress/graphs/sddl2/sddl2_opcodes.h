// Copyright (c) Meta Platforms, Inc. and affiliates.

// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY
//
// Generated from: src/openzl/compress/graphs/sddl2/sddl2_opcodes.def
// Generated at: 2025-11-21 17:16:01 UTC
// Generator: generate_c_headers.py
//
// To regenerate: python3 src/openzl/compress/graphs/sddl2/generate_c_headers.py

#ifndef OPENZL_SDDL2_OPCODES_H
#define OPENZL_SDDL2_OPCODES_H

/**
 * SDDL2 VM Opcode Definitions
 *
 * This file defines the opcode families and instruction opcodes for the SDDL2
 * VM.
 *
 * Instruction Format:
 * - 32-bit instruction word (little-endian)
 * - Low 16 bits: Family ID
 * - High 16 bits: Opcode within family
 */

/* ============================================================================
 * OPCODE FAMILIES
 * ========================================================================= */

enum sddl2_family {
    SDDL2_FAMILY_PUSH = 0x0001, /* Push constants and values onto stack */
    SDDL2_FAMILY_MATH = 0x0002, /* Arithmetic operations on I64 values */
    SDDL2_FAMILY_CMP  = 0x0003, /* Comparison operations on signed I64 values */
    SDDL2_FAMILY_LOGIC   = 0x0004, /* Logical operations */
    SDDL2_FAMILY_CONTROL = 0x0005, /* Control flow operations */
    SDDL2_FAMILY_LOAD    = 0x0006, /* Load operations */
    SDDL2_FAMILY_STACK   = 0x0007, /* Stack manipulation operations */
    SDDL2_FAMILY_TYPE    = 0x0008, /* Type operations */
    SDDL2_FAMILY_VAR     = 0x0009, /* Variable operations */
    SDDL2_FAMILY_CALL    = 0x000B, /* Function call operations */
    SDDL2_FAMILY_SEGMENT = 0x000C, /* Segment creation operations */
};

/* ============================================================================
 * OPCODES
 * ========================================================================= */

/* PUSH family (0x0001) - Push constants and values onto stack */
enum sddl2_opcode_push {
    SDDL2_OP_PUSH_ZERO        = 0x0001,
    SDDL2_OP_PUSH_U32         = 0x0002, /* param: u32 */
    SDDL2_OP_PUSH_I32         = 0x0003, /* param: i32 */
    SDDL2_OP_PUSH_I64         = 0x0004, /* param: i64 */
    SDDL2_OP_PUSH_TAG         = 0x0005, /* param: u32 */
    SDDL2_OP_PUSH_CURRENT_POS = 0x0080,
    SDDL2_OP_PUSH_REMAINING   = 0x0081,
    SDDL2_OP_PUSH_STACK_DEPTH = 0x0082,
    SDDL2_OP_PUSH_TYPE_BYTES  = 0x0100,
    SDDL2_OP_PUSH_TYPE_U8     = 0x0110,
    SDDL2_OP_PUSH_TYPE_I8     = 0x0111,
    SDDL2_OP_PUSH_TYPE_U16LE  = 0x0112,
    SDDL2_OP_PUSH_TYPE_U16BE  = 0x0113,
    SDDL2_OP_PUSH_TYPE_I16LE  = 0x0114,
    SDDL2_OP_PUSH_TYPE_I16BE  = 0x0115,
    SDDL2_OP_PUSH_TYPE_U32LE  = 0x0116,
    SDDL2_OP_PUSH_TYPE_U32BE  = 0x0117,
    SDDL2_OP_PUSH_TYPE_I32LE  = 0x0118,
    SDDL2_OP_PUSH_TYPE_I32BE  = 0x0119,
    SDDL2_OP_PUSH_TYPE_U64LE  = 0x011A,
    SDDL2_OP_PUSH_TYPE_U64BE  = 0x011B,
    SDDL2_OP_PUSH_TYPE_I64LE  = 0x011C,
    SDDL2_OP_PUSH_TYPE_I64BE  = 0x011D,
    SDDL2_OP_PUSH_TYPE_F8     = 0x0130,
    SDDL2_OP_PUSH_TYPE_F16LE  = 0x0131,
    SDDL2_OP_PUSH_TYPE_F16BE  = 0x0132,
    SDDL2_OP_PUSH_TYPE_BF16LE = 0x0133,
    SDDL2_OP_PUSH_TYPE_BF16BE = 0x0134,
    SDDL2_OP_PUSH_TYPE_F32LE  = 0x0135,
    SDDL2_OP_PUSH_TYPE_F32BE  = 0x0136,
    SDDL2_OP_PUSH_TYPE_F64LE  = 0x0137,
    SDDL2_OP_PUSH_TYPE_F64BE  = 0x0138,
};

/* MATH family (0x0002) - Arithmetic operations on I64 values */
enum sddl2_opcode_math {
    SDDL2_OP_MATH_ADD = 0x0001,
    SDDL2_OP_MATH_SUB = 0x0002,
    SDDL2_OP_MATH_MUL = 0x0003,
    SDDL2_OP_MATH_DIV = 0x0004,
    SDDL2_OP_MATH_MOD = 0x0005,
    SDDL2_OP_MATH_ABS = 0x0006,
    SDDL2_OP_MATH_NEG = 0x0007,
};

/* CMP family (0x0003) - Comparison operations on signed I64 values */
enum sddl2_opcode_cmp {
    SDDL2_OP_CMP_EQ = 0x0001,
    SDDL2_OP_CMP_NE = 0x0002,
    SDDL2_OP_CMP_LT = 0x0003,
    SDDL2_OP_CMP_LE = 0x0004,
    SDDL2_OP_CMP_GT = 0x0005,
    SDDL2_OP_CMP_GE = 0x0006,
};

/* LOGIC family (0x0004) - Logical operations */
enum sddl2_opcode_logic {
    SDDL2_OP_LOGIC_AND = 0x0001,
    SDDL2_OP_LOGIC_OR  = 0x0002,
    SDDL2_OP_LOGIC_XOR = 0x0003,
    SDDL2_OP_LOGIC_NOT = 0x0004,
};

/* CONTROL family (0x0005) - Control flow operations */
enum sddl2_opcode_control {
    SDDL2_OP_CONTROL_HALT        = 0x0001,
    SDDL2_OP_CONTROL_EXPECT_TRUE = 0x0002,
    SDDL2_OP_CONTROL_TRACE_START = 0x0004,
};

/* LOAD family (0x0006) - Load operations */
enum sddl2_opcode_load {
    SDDL2_OP_LOAD_U8    = 0x0001,
    SDDL2_OP_LOAD_I8    = 0x0002,
    SDDL2_OP_LOAD_U16LE = 0x0010,
    SDDL2_OP_LOAD_I16LE = 0x0011,
    SDDL2_OP_LOAD_U32LE = 0x0020,
    SDDL2_OP_LOAD_I32LE = 0x0021,
    SDDL2_OP_LOAD_I64LE = 0x0030,
    SDDL2_OP_LOAD_U16BE = 0x0110,
    SDDL2_OP_LOAD_I16BE = 0x0111,
    SDDL2_OP_LOAD_U32BE = 0x0120,
    SDDL2_OP_LOAD_I32BE = 0x0121,
    SDDL2_OP_LOAD_I64BE = 0x0130,
};

/* STACK family (0x0007) - Stack manipulation operations */
enum sddl2_opcode_stack {
    SDDL2_OP_STACK_DUP     = 0x0001,
    SDDL2_OP_STACK_OVER    = 0x0002,
    SDDL2_OP_STACK_DROP    = 0x0003,
    SDDL2_OP_STACK_SWAP    = 0x0004,
    SDDL2_OP_STACK_ROT     = 0x0005,
    SDDL2_OP_STACK_DROP_IF = 0x0010,
};

/* TYPE family (0x0008) - Type operations */
enum sddl2_opcode_type {
    SDDL2_OP_TYPE_FIXED_ARRAY = 0x0001,
    SDDL2_OP_TYPE_STRUCTURE   = 0x0002,
    SDDL2_OP_TYPE_SIZEOF      = 0x0010,
};

/* SEGMENT family (0x000C) - Segment creation operations */
enum sddl2_opcode_segment {
    SDDL2_OP_SEGMENT_CREATE_UNSPECIFIED = 0x0001,
    SDDL2_OP_SEGMENT_CREATE_TAGGED      = 0x0002,
};

#endif // OPENZL_SDDL2_OPCODES_H
