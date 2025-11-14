// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * SDDL2 Disassembler - Implementation
 *
 * Provides instruction name decoding for debugging purposes.
 * Mnemonics match the SDDL2 assembly syntax defined in sddl2_opcodes.def.
 */

#include "sddl2_vm.h"
#include "sddl2_disasm.h"
#include "openzl/compress/graphs/sddl2/sddl2_opcodes.h"
#include "openzl/common/logging.h"

const char* SDDL2_instruction_name(uint16_t family, uint16_t opcode)
{
    switch (family) {
        case SDDL2_FAMILY_PUSH:
            switch (opcode) {
                case SDDL2_OP_PUSH_ZERO: return "push.zero";
                case SDDL2_OP_PUSH_U32: return "push.u32";
                case SDDL2_OP_PUSH_I32: return "push.i32";
                case SDDL2_OP_PUSH_I64: return "push.i64";
                case SDDL2_OP_PUSH_TAG: return "push.tag";
                case SDDL2_OP_PUSH_CURRENT_POS: return "push.current_pos";
                case SDDL2_OP_PUSH_REMAINING: return "push.remaining";
                case SDDL2_OP_PUSH_STACK_DEPTH: return "push.stack_depth";
                case SDDL2_OP_PUSH_TYPE_BYTES: return "push.type.bytes";
                case SDDL2_OP_PUSH_TYPE_U8: return "push.type.u8";
                case SDDL2_OP_PUSH_TYPE_I8: return "push.type.i8";
                case SDDL2_OP_PUSH_TYPE_U16LE: return "push.type.u16le";
                case SDDL2_OP_PUSH_TYPE_U16BE: return "push.type.u16be";
                case SDDL2_OP_PUSH_TYPE_I16LE: return "push.type.i16le";
                case SDDL2_OP_PUSH_TYPE_I16BE: return "push.type.i16be";
                case SDDL2_OP_PUSH_TYPE_U32LE: return "push.type.u32le";
                case SDDL2_OP_PUSH_TYPE_U32BE: return "push.type.u32be";
                case SDDL2_OP_PUSH_TYPE_I32LE: return "push.type.i32le";
                case SDDL2_OP_PUSH_TYPE_I32BE: return "push.type.i32be";
                case SDDL2_OP_PUSH_TYPE_U64LE: return "push.type.u64le";
                case SDDL2_OP_PUSH_TYPE_U64BE: return "push.type.u64be";
                case SDDL2_OP_PUSH_TYPE_I64LE: return "push.type.i64le";
                case SDDL2_OP_PUSH_TYPE_I64BE: return "push.type.i64be";
                case SDDL2_OP_PUSH_TYPE_F8: return "push.type.f8";
                case SDDL2_OP_PUSH_TYPE_F16LE: return "push.type.f16le";
                case SDDL2_OP_PUSH_TYPE_F16BE: return "push.type.f16be";
                case SDDL2_OP_PUSH_TYPE_BF16LE: return "push.type.bf16le";
                case SDDL2_OP_PUSH_TYPE_BF16BE: return "push.type.bf16be";
                case SDDL2_OP_PUSH_TYPE_F32LE: return "push.type.f32le";
                case SDDL2_OP_PUSH_TYPE_F32BE: return "push.type.f32be";
                case SDDL2_OP_PUSH_TYPE_F64LE: return "push.type.f64le";
                case SDDL2_OP_PUSH_TYPE_F64BE: return "push.type.f64be";
                default: return "push.?";
            }

        case SDDL2_FAMILY_MATH:
            switch (opcode) {
                case SDDL2_OP_MATH_ADD: return "math.add";
                case SDDL2_OP_MATH_SUB: return "math.sub";
                case SDDL2_OP_MATH_MUL: return "math.mul";
                case SDDL2_OP_MATH_DIV: return "math.div";
                case SDDL2_OP_MATH_MOD: return "math.mod";
                case SDDL2_OP_MATH_ABS: return "math.abs";
                case SDDL2_OP_MATH_NEG: return "math.neg";
                default: return "math.?";
            }

        case SDDL2_FAMILY_CMP:
            switch (opcode) {
                case SDDL2_OP_CMP_EQ: return "cmp.eq";
                case SDDL2_OP_CMP_NE: return "cmp.ne";
                case SDDL2_OP_CMP_LT: return "cmp.lt";
                case SDDL2_OP_CMP_LE: return "cmp.le";
                case SDDL2_OP_CMP_GT: return "cmp.gt";
                case SDDL2_OP_CMP_GE: return "cmp.ge";
                default: return "cmp.?";
            }

        case SDDL2_FAMILY_LOGIC:
            switch (opcode) {
                case SDDL2_OP_LOGIC_AND: return "logic.and";
                case SDDL2_OP_LOGIC_OR: return "logic.or";
                case SDDL2_OP_LOGIC_XOR: return "logic.xor";
                case SDDL2_OP_LOGIC_NOT: return "logic.not";
                default: return "logic.?";
            }

        case SDDL2_FAMILY_CONTROL:
            switch (opcode) {
                case SDDL2_OP_CONTROL_HALT: return "halt";
                case SDDL2_OP_CONTROL_EXPECT_TRUE: return "expect_true";
                default: return "control.?";
            }

        case SDDL2_FAMILY_LOAD:
            switch (opcode) {
                case SDDL2_OP_LOAD_U8: return "load.u8";
                case SDDL2_OP_LOAD_I8: return "load.i8";
                case SDDL2_OP_LOAD_U16LE: return "load.u16le";
                case SDDL2_OP_LOAD_I16LE: return "load.i16le";
                case SDDL2_OP_LOAD_U32LE: return "load.u32le";
                case SDDL2_OP_LOAD_I32LE: return "load.i32le";
                case SDDL2_OP_LOAD_I64LE: return "load.i64le";
                case SDDL2_OP_LOAD_U16BE: return "load.u16be";
                case SDDL2_OP_LOAD_I16BE: return "load.i16be";
                case SDDL2_OP_LOAD_U32BE: return "load.u32be";
                case SDDL2_OP_LOAD_I32BE: return "load.i32be";
                case SDDL2_OP_LOAD_I64BE: return "load.i64be";
                default: return "load.?";
            }

        case SDDL2_FAMILY_STACK:
            switch (opcode) {
                case SDDL2_OP_STACK_DUP: return "stack.dup";
                case SDDL2_OP_STACK_DROP: return "stack.drop";
                case SDDL2_OP_STACK_SWAP: return "stack.swap";
                case SDDL2_OP_STACK_DROP_IF: return "stack.drop_if";
                default: return "stack.?";
            }

        case SDDL2_FAMILY_TYPE:
            switch (opcode) {
                case SDDL2_OP_TYPE_FIXED_ARRAY: return "type.fixed_array";
                case SDDL2_OP_TYPE_STRUCTURE: return "type.structure";
                default: return "type.?";
            }

        case SDDL2_FAMILY_SEGMENT:
            switch (opcode) {
                case SDDL2_OP_SEGMENT_CREATE_UNSPECIFIED:
                    return "segment.create_unspecified";
                case SDDL2_OP_SEGMENT_CREATE_TAGGED:
                    return "segment.create_tagged";
                default: return "segment.?";
            }

        case SDDL2_FAMILY_VAR:
            return "var.?";

        case SDDL2_FAMILY_CALL:
            return "call.?";

        default:
            return "?.?";
    }
}

void SDDL2_log_expect_true_failure(int64_t value, const SDDL2_Stack* stack)
{
    ZL_DLOG(ERROR, "========================================");
    ZL_DLOG(ERROR, "[SDDL2] expect_true VALIDATION FAILURE");
    ZL_DLOG(ERROR, "========================================");
    ZL_DLOG(ERROR, "Checked value: %lld (0x%llx)", 
            (long long)value, (unsigned long long)value);
    ZL_DLOG(ERROR, "Expected: non-zero value (true)");
    ZL_DLOG(ERROR, "Actual: 0 (false)");
    ZL_DLOG(ERROR, "");
    ZL_DLOG(ERROR, "Remaining stack after pop:");
    ZL_DLOG(ERROR, "  Stack depth: %zu", stack->top);
    
    if (stack->top > 0) {
        size_t show_count = stack->top < 5 ? stack->top : 5;
        for (size_t i = 0; i < show_count; i++) {
            size_t idx = stack->top - 1 - i;
            const SDDL2_Value* val = &stack->items[idx];
            
            switch (val->kind) {
                case SDDL2_VALUE_I64:
                    ZL_DLOG(ERROR, "  [%zu] I64: %lld (0x%llx)", 
                            idx, 
                            (long long)val->value.as_i64,
                            (unsigned long long)val->value.as_i64);
                    break;
                case SDDL2_VALUE_TAG:
                    ZL_DLOG(ERROR, "  [%zu] TAG: %u", idx, val->value.as_tag);
                    break;
                case SDDL2_VALUE_TYPE:
                    ZL_DLOG(ERROR, "  [%zu] TYPE: kind=%d width=%u", 
                            idx, 
                            val->value.as_type.kind, 
                            val->value.as_type.width);
                    break;
            }
        }
        if (stack->top > 5) {
            ZL_DLOG(ERROR, "  ... and %zu more values", stack->top - 5);
        }
    } else {
        ZL_DLOG(ERROR, "  (empty)");
    }
    
    ZL_DLOG(ERROR, "========================================");
}
