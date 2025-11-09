// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/graphs/sddlv2/sddl2_interpreter.h"
#include "openzl/compress/graphs/sddlv2/sddl2_opcodes.h"
#include "openzl/shared/mem.h"

/* ============================================================================
 * Push Type Opcode Lookup Table
 * ========================================================================= */

/**
 * Lookup table mapping push.type opcodes to their corresponding type kinds.
 * This table provides a compact, maintainable mapping for all 24 type opcodes
 * instead of 24 individual else-if branches.
 */
static const struct {
    uint16_t opcode;
    SDDL2_type_kind kind;
} PUSH_TYPE_MAP[] = {
    { SDDL2_OP_PUSH_TYPE_BYTES, SDDL2_TYPE_BYTES },
    { SDDL2_OP_PUSH_TYPE_U8, SDDL2_TYPE_U8 },
    { SDDL2_OP_PUSH_TYPE_I8, SDDL2_TYPE_I8 },
    { SDDL2_OP_PUSH_TYPE_U16LE, SDDL2_TYPE_U16LE },
    { SDDL2_OP_PUSH_TYPE_U16BE, SDDL2_TYPE_U16BE },
    { SDDL2_OP_PUSH_TYPE_I16LE, SDDL2_TYPE_I16LE },
    { SDDL2_OP_PUSH_TYPE_I16BE, SDDL2_TYPE_I16BE },
    { SDDL2_OP_PUSH_TYPE_U32LE, SDDL2_TYPE_U32LE },
    { SDDL2_OP_PUSH_TYPE_U32BE, SDDL2_TYPE_U32BE },
    { SDDL2_OP_PUSH_TYPE_I32LE, SDDL2_TYPE_I32LE },
    { SDDL2_OP_PUSH_TYPE_I32BE, SDDL2_TYPE_I32BE },
    { SDDL2_OP_PUSH_TYPE_U64LE, SDDL2_TYPE_U64LE },
    { SDDL2_OP_PUSH_TYPE_U64BE, SDDL2_TYPE_U64BE },
    { SDDL2_OP_PUSH_TYPE_I64LE, SDDL2_TYPE_I64LE },
    { SDDL2_OP_PUSH_TYPE_I64BE, SDDL2_TYPE_I64BE },
    { SDDL2_OP_PUSH_TYPE_F8, SDDL2_TYPE_F8 },
    { SDDL2_OP_PUSH_TYPE_F16LE, SDDL2_TYPE_F16LE },
    { SDDL2_OP_PUSH_TYPE_F16BE, SDDL2_TYPE_F16BE },
    { SDDL2_OP_PUSH_TYPE_BF16LE, SDDL2_TYPE_BF16LE },
    { SDDL2_OP_PUSH_TYPE_BF16BE, SDDL2_TYPE_BF16BE },
    { SDDL2_OP_PUSH_TYPE_F32LE, SDDL2_TYPE_F32LE },
    { SDDL2_OP_PUSH_TYPE_F32BE, SDDL2_TYPE_F32BE },
    { SDDL2_OP_PUSH_TYPE_F64LE, SDDL2_TYPE_F64LE },
    { SDDL2_OP_PUSH_TYPE_F64BE, SDDL2_TYPE_F64BE },
};

#define PUSH_TYPE_MAP_SIZE (sizeof(PUSH_TYPE_MAP) / sizeof(PUSH_TYPE_MAP[0]))

SDDL2_error SDDL2_execute_bytecode(
        const void* bytecode_buffer,
        size_t bytecode_size,
        const void* input_data,
        size_t input_size,
        SDDL2_segment_list* output_segments)
{
    const char* bytecode = bytecode_buffer;

    // Validate input conditions
    ZL_ASSERT_NN(output_segments);
    if (bytecode == NULL)
        ZL_ASSERT_EQ(bytecode_size, 0);

    if (bytecode_size % 4 != 0) {
        // Bytecode must be multiple of 4
        return SDDL2_INVALID_BYTECODE;
    }

    // Initialize VM state
    SDDL2_stack stack;
    SDDL2_value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_stack_init(&stack);

    SDDL2_input_buffer buffer;
    SDDL2_input_buffer_init(&buffer, input_data, input_size);

    SDDL2_tag_registry registry;
    // Use same allocator as output_segments (arena in production, NULL in
    // tests)
    SDDL2_tag_registry_init(
            &registry, output_segments->alloc_fn, output_segments->alloc_ctx);

    // Program counter (byte offset)
    size_t pc = 0;

    // Execution loop
    int halted = 0;
    while (pc < bytecode_size && !halted) {
        // Fetch instruction (32-bit word)
        if (pc + 4 > bytecode_size) {
            SDDL2_tag_registry_destroy(&registry);
            return SDDL2_INVALID_BYTECODE; // Incomplete instruction
        }

        uint32_t instruction = ZL_readLE32(&bytecode[pc]);
        pc += 4;

        // Decode instruction word
        // Bits 31-16: Family ID
        // Bits 15-0:  Opcode within family
        uint16_t family = (instruction >> 16) & 0xFFFF;
        uint16_t opcode = instruction & 0xFFFF;

        // Dispatch
        SDDL2_error err = SDDL2_OK;

        switch (family) {
            case SDDL2_FAMILY_CONTROL:
                if (opcode == SDDL2_OP_CONTROL_HALT) {
                    halted = 1;
                } else {
                    SDDL2_tag_registry_destroy(&registry);
                    return SDDL2_INVALID_BYTECODE; // Unknown opcode
                }
                break;

            case SDDL2_FAMILY_PUSH:
                if (opcode == SDDL2_OP_PUSH_ZERO) {
                    err = SDDL2_stack_push(&stack, SDDL2_value_i64(0));
                } else if (opcode == SDDL2_OP_PUSH_U32) {
                    if (pc + 4 > bytecode_size) {
                        SDDL2_tag_registry_destroy(&registry);
                        return SDDL2_INVALID_BYTECODE; // Missing immediate
                    }
                    uint32_t value = ZL_readLE32(&bytecode[pc]);
                    pc += 4;
                    err = SDDL2_stack_push(
                            &stack, SDDL2_value_i64((int64_t)value));
                } else if (opcode == SDDL2_OP_PUSH_I32) {
                    if (pc + 4 > bytecode_size) {
                        SDDL2_tag_registry_destroy(&registry);
                        return SDDL2_INVALID_BYTECODE; // Missing immediate
                    }
                    int32_t value = (int32_t)ZL_readLE32(&bytecode[pc]);
                    pc += 4;
                    err = SDDL2_stack_push(
                            &stack, SDDL2_value_i64((int64_t)value));
                } else if (opcode == SDDL2_OP_PUSH_I64) {
                    if (pc + 8 > bytecode_size) {
                        SDDL2_tag_registry_destroy(&registry);
                        return SDDL2_INVALID_BYTECODE; // Missing immediate
                    }
                    int64_t value = (int64_t)ZL_readLE64(&bytecode[pc]);
                    pc += 8;
                    err = SDDL2_stack_push(&stack, SDDL2_value_i64(value));
                } else if (opcode == SDDL2_OP_PUSH_TAG) {
                    // Read tag immediate (u32)
                    if (pc + 4 > bytecode_size) {
                        SDDL2_tag_registry_destroy(&registry);
                        return SDDL2_INVALID_BYTECODE; // Missing immediate
                    }
                    uint32_t tag = ZL_readLE32(&bytecode[pc]);
                    pc += 4;
                    err = SDDL2_stack_push(&stack, SDDL2_value_tag(tag));
                } else {
                    // Handle all push.type opcodes via lookup table
                    int found = 0;
                    for (size_t i = 0; i < PUSH_TYPE_MAP_SIZE; i++) {
                        if (opcode == PUSH_TYPE_MAP[i].opcode) {
                            SDDL2_type type = { .kind  = PUSH_TYPE_MAP[i].kind,
                                                .width = 1 };
                            err             = SDDL2_stack_push(
                                    &stack, SDDL2_value_type(type));
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        SDDL2_tag_registry_destroy(&registry);
                        return SDDL2_INVALID_BYTECODE; // Unknown opcode
                    }
                }
                break;

            case SDDL2_FAMILY_SEGMENT:
                if (opcode == SDDL2_OP_SEGMENT_CREATE_UNSPECIFIED) {
                    err = SDDL2_op_segment_create_unspecified(
                            &stack, &buffer, output_segments);
                } else if (opcode == SDDL2_OP_SEGMENT_CREATE_TAGGED) {
                    err = SDDL2_op_segment_create_tagged(
                            &stack, &buffer, output_segments, &registry);
                } else {
                    SDDL2_tag_registry_destroy(&registry);
                    return SDDL2_INVALID_BYTECODE; // Unknown opcode
                }
                break;

            case SDDL2_FAMILY_MATH:
                if (opcode == SDDL2_OP_MATH_ADD) {
                    err = SDDL2_op_add(&stack);
                } else if (opcode == SDDL2_OP_MATH_SUB) {
                    err = SDDL2_op_sub(&stack);
                } else if (opcode == SDDL2_OP_MATH_MUL) {
                    err = SDDL2_op_mul(&stack);
                } else if (opcode == SDDL2_OP_MATH_DIV) {
                    err = SDDL2_op_div(&stack);
                } else if (opcode == SDDL2_OP_MATH_MOD) {
                    err = SDDL2_op_mod(&stack);
                } else if (opcode == SDDL2_OP_MATH_ABS) {
                    err = SDDL2_op_abs(&stack);
                } else if (opcode == SDDL2_OP_MATH_NEG) {
                    err = SDDL2_op_neg(&stack);
                } else {
                    SDDL2_tag_registry_destroy(&registry);
                    return SDDL2_INVALID_BYTECODE; // Unknown opcode
                }
                break;

            // Unimplemented families
            case SDDL2_FAMILY_CMP:
            case SDDL2_FAMILY_LOGIC:
            case SDDL2_FAMILY_LOAD:
            case SDDL2_FAMILY_STACK:
            case SDDL2_FAMILY_TYPE:
            case SDDL2_FAMILY_VAR:
            case SDDL2_FAMILY_EXPECT:
            case SDDL2_FAMILY_CALL:
                SDDL2_tag_registry_destroy(&registry);
                return SDDL2_INVALID_BYTECODE; // Unimplemented family
        }

        // Check for errors
        if (err != SDDL2_OK) {
            SDDL2_tag_registry_destroy(&registry);
            return err;
        }
    }

    // Cleanup
    SDDL2_tag_registry_destroy(&registry);

    if (!halted) {
        return SDDL2_INVALID_BYTECODE; // Program didn't halt properly
    }

    return SDDL2_OK;
}
