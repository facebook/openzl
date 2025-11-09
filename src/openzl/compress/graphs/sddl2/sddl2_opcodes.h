// Copyright (c) Meta Platforms, Inc. and affiliates.

// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY
//
// Generated from: src/openzl/compress/graphs/sddlv2/sddl2_opcodes.def
// Generated at: 2025-11-09 15:02:08 UTC
// Generator: generate_opcodes.py
//
// To regenerate: python3 tools/sddl/assembler/generate_opcodes.py

#ifndef OPENZL_SDDL2_OPCODES_H
#define OPENZL_SDDL2_OPCODES_H

/**
 * SDDL2 VM Opcode Definitions
 *
 * This file defines the opcode families and instruction opcodes for the SDDL2 VM.
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
    SDDL2_FAMILY_PUSH     = 0x0001,  /* Push constants and values onto stack */
    SDDL2_FAMILY_MATH     = 0x0002,  /* Arithmetic operations on I64 values */
    SDDL2_FAMILY_CMP      = 0x0003,  /* Comparison operations on signed I64 values */
    SDDL2_FAMILY_LOGIC    = 0x0004,  /* Logical operations */
    SDDL2_FAMILY_CONTROL  = 0x0005,  /* Control flow operations */
    SDDL2_FAMILY_LOAD     = 0x0006,  /* Load operations */
    SDDL2_FAMILY_STACK    = 0x0007,  /* Stack manipulation operations */
    SDDL2_FAMILY_TYPE     = 0x0008,  /* Type operations */
    SDDL2_FAMILY_VAR      = 0x0009,  /* Variable operations */
    SDDL2_FAMILY_EXPECT   = 0x000A,  /* Expect/validation operations */
    SDDL2_FAMILY_CALL     = 0x000B,  /* Function call operations */
    SDDL2_FAMILY_SEGMENT  = 0x000C,  /* Segment creation operations */
};

/* ============================================================================
 * OPCODES
 * ========================================================================= */

/* PUSH family (0x0001) - Push constants and values onto stack */
enum sddl2_opcode_push {
    SDDL2_OP_PUSH_ZERO = 0x0001,
    SDDL2_OP_PUSH_U32 = 0x0002,  /* param: u32 */
    SDDL2_OP_PUSH_I32 = 0x0003,  /* param: i32 */
    SDDL2_OP_PUSH_I64 = 0x0004,  /* param: i64 */
    SDDL2_OP_PUSH_TAG = 0x0005,  /* param: u32 */
    SDDL2_OP_PUSH_TYPE_BYTES = 0x0100,
    SDDL2_OP_PUSH_TYPE_U8 = 0x0110,
    SDDL2_OP_PUSH_TYPE_I8 = 0x0111,
    SDDL2_OP_PUSH_TYPE_U16LE = 0x0112,
    SDDL2_OP_PUSH_TYPE_U16BE = 0x0113,
    SDDL2_OP_PUSH_TYPE_I16LE = 0x0114,
    SDDL2_OP_PUSH_TYPE_I16BE = 0x0115,
    SDDL2_OP_PUSH_TYPE_U32LE = 0x0116,
    SDDL2_OP_PUSH_TYPE_U32BE = 0x0117,
    SDDL2_OP_PUSH_TYPE_I32LE = 0x0118,
    SDDL2_OP_PUSH_TYPE_I32BE = 0x0119,
    SDDL2_OP_PUSH_TYPE_U64LE = 0x011A,
    SDDL2_OP_PUSH_TYPE_U64BE = 0x011B,
    SDDL2_OP_PUSH_TYPE_I64LE = 0x011C,
    SDDL2_OP_PUSH_TYPE_I64BE = 0x011D,
    SDDL2_OP_PUSH_TYPE_F8 = 0x0130,
    SDDL2_OP_PUSH_TYPE_F16LE = 0x0131,
    SDDL2_OP_PUSH_TYPE_F16BE = 0x0132,
    SDDL2_OP_PUSH_TYPE_BF16LE = 0x0133,
    SDDL2_OP_PUSH_TYPE_BF16BE = 0x0134,
    SDDL2_OP_PUSH_TYPE_F32LE = 0x0135,
    SDDL2_OP_PUSH_TYPE_F32BE = 0x0136,
    SDDL2_OP_PUSH_TYPE_F64LE = 0x0137,
    SDDL2_OP_PUSH_TYPE_F64BE = 0x0138,
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

/* CONTROL family (0x0005) - Control flow operations */
enum sddl2_opcode_control {
    SDDL2_OP_CONTROL_HALT = 0x0001,
};

/* STACK family (0x0007) - Stack manipulation operations */
enum sddl2_opcode_stack {
    SDDL2_OP_STACK_DUP = 0x0001,
    SDDL2_OP_STACK_OVER = 0x0002,
    SDDL2_OP_STACK_DROP = 0x0003,
    SDDL2_OP_STACK_SWAP = 0x0004,
    SDDL2_OP_STACK_ROT = 0x0005,
};

/* TYPE family (0x0008) - Type operations */
enum sddl2_opcode_type {
    SDDL2_OP_TYPE_CONST = 0x0001,  /* param: u32, u32 */
};

/* SEGMENT family (0x000C) - Segment creation operations */
enum sddl2_opcode_segment {
    SDDL2_OP_SEGMENT_CREATE_UNSPECIFIED = 0x0001,
    SDDL2_OP_SEGMENT_CREATE_TAGGED = 0x0002,
};

#endif // OPENZL_SDDL2_OPCODES_H
