// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/graphs/sddl2/sddl2_interpreter.h"
#include "openzl/compress/graphs/sddl2/sddl2_opcodes.h"
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

/* ============================================================================
 * Operation Dispatch Tables
 * ========================================================================= */

/**
 * Function pointer type for stack operations.
 * All MATH, CMP, and STACK operations share this signature.
 */
typedef SDDL2_error (*SDDL2_stack_op_fn)(SDDL2_stack*);

/**
 * Lookup table for MATH family operations.
 */
static const struct {
    uint16_t opcode;
    SDDL2_stack_op_fn handler;
} MATH_OP_MAP[] = {
    { SDDL2_OP_MATH_ADD, SDDL2_op_add }, { SDDL2_OP_MATH_SUB, SDDL2_op_sub },
    { SDDL2_OP_MATH_MUL, SDDL2_op_mul }, { SDDL2_OP_MATH_DIV, SDDL2_op_div },
    { SDDL2_OP_MATH_MOD, SDDL2_op_mod }, { SDDL2_OP_MATH_ABS, SDDL2_op_abs },
    { SDDL2_OP_MATH_NEG, SDDL2_op_neg },
};

#define MATH_OP_MAP_SIZE (sizeof(MATH_OP_MAP) / sizeof(MATH_OP_MAP[0]))

/**
 * Lookup table for CMP family operations.
 */
static const struct {
    uint16_t opcode;
    SDDL2_stack_op_fn handler;
} CMP_OP_MAP[] = {
    { SDDL2_OP_CMP_EQ, SDDL2_op_eq }, { SDDL2_OP_CMP_NE, SDDL2_op_ne },
    { SDDL2_OP_CMP_LT, SDDL2_op_lt }, { SDDL2_OP_CMP_LE, SDDL2_op_le },
    { SDDL2_OP_CMP_GT, SDDL2_op_gt }, { SDDL2_OP_CMP_GE, SDDL2_op_ge },
};

#define CMP_OP_MAP_SIZE (sizeof(CMP_OP_MAP) / sizeof(CMP_OP_MAP[0]))

/**
 * Lookup table for STACK family operations.
 */
static const struct {
    uint16_t opcode;
    SDDL2_stack_op_fn handler;
} STACK_OP_MAP[] = {
    { SDDL2_OP_STACK_DROP, SDDL2_op_drop },
    { SDDL2_OP_STACK_DUP, SDDL2_op_dup },
    { SDDL2_OP_STACK_SWAP, SDDL2_op_swap },
};

#define STACK_OP_MAP_SIZE (sizeof(STACK_OP_MAP) / sizeof(STACK_OP_MAP[0]))

/**
 * Cleanup helper for SDDL2_execute_bytecode.
 * Destroys the tag registry and returns the specified error code.
 *
 * This macro is function-scoped and undefined after SDDL2_execute_bytecode.
 */
#define CLEANUP_AND_RETURN(error_code)         \
    do {                                       \
        SDDL2_tag_registry_destroy(&registry); \
        return (error_code);                   \
    } while (0)

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
            CLEANUP_AND_RETURN(
                    SDDL2_INVALID_BYTECODE); // Incomplete instruction
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
                    CLEANUP_AND_RETURN(
                            SDDL2_INVALID_BYTECODE); // Unknown opcode
                }
                break;

            case SDDL2_FAMILY_PUSH:
                if (opcode == SDDL2_OP_PUSH_ZERO) {
                    err = SDDL2_stack_push(&stack, SDDL2_value_i64(0));
                } else if (opcode == SDDL2_OP_PUSH_U32) {
                    if (pc + 4 > bytecode_size) {
                        CLEANUP_AND_RETURN(
                                SDDL2_INVALID_BYTECODE); // Missing immediate
                    }
                    uint32_t value = ZL_readLE32(&bytecode[pc]);
                    pc += 4;
                    err = SDDL2_stack_push(
                            &stack, SDDL2_value_i64((int64_t)value));
                } else if (opcode == SDDL2_OP_PUSH_I32) {
                    if (pc + 4 > bytecode_size) {
                        CLEANUP_AND_RETURN(
                                SDDL2_INVALID_BYTECODE); // Missing immediate
                    }
                    int32_t value = (int32_t)ZL_readLE32(&bytecode[pc]);
                    pc += 4;
                    err = SDDL2_stack_push(
                            &stack, SDDL2_value_i64((int64_t)value));
                } else if (opcode == SDDL2_OP_PUSH_I64) {
                    if (pc + 8 > bytecode_size) {
                        CLEANUP_AND_RETURN(
                                SDDL2_INVALID_BYTECODE); // Missing immediate
                    }
                    int64_t value = (int64_t)ZL_readLE64(&bytecode[pc]);
                    pc += 8;
                    err = SDDL2_stack_push(&stack, SDDL2_value_i64(value));
                } else if (opcode == SDDL2_OP_PUSH_TAG) {
                    // Read tag immediate (u32)
                    if (pc + 4 > bytecode_size) {
                        CLEANUP_AND_RETURN(
                                SDDL2_INVALID_BYTECODE); // Missing immediate
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
                        CLEANUP_AND_RETURN(
                                SDDL2_INVALID_BYTECODE); // Unknown opcode
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
                    CLEANUP_AND_RETURN(
                            SDDL2_INVALID_BYTECODE); // Unknown opcode
                }
                break;

            case SDDL2_FAMILY_MATH: {
                // Dispatch via lookup table
                SDDL2_stack_op_fn handler = NULL;
                for (size_t i = 0; i < MATH_OP_MAP_SIZE; i++) {
                    if (opcode == MATH_OP_MAP[i].opcode) {
                        handler = MATH_OP_MAP[i].handler;
                        break;
                    }
                }
                if (handler) {
                    err = handler(&stack);
                } else {
                    CLEANUP_AND_RETURN(
                            SDDL2_INVALID_BYTECODE); // Unknown opcode
                }
                break;
            }

            case SDDL2_FAMILY_CMP: {
                // Dispatch via lookup table
                SDDL2_stack_op_fn handler = NULL;
                for (size_t i = 0; i < CMP_OP_MAP_SIZE; i++) {
                    if (opcode == CMP_OP_MAP[i].opcode) {
                        handler = CMP_OP_MAP[i].handler;
                        break;
                    }
                }
                if (handler) {
                    err = handler(&stack);
                } else {
                    CLEANUP_AND_RETURN(
                            SDDL2_INVALID_BYTECODE); // Unknown opcode
                }
                break;
            }

            case SDDL2_FAMILY_STACK: {
                // Dispatch via lookup table
                SDDL2_stack_op_fn handler = NULL;
                for (size_t i = 0; i < STACK_OP_MAP_SIZE; i++) {
                    if (opcode == STACK_OP_MAP[i].opcode) {
                        handler = STACK_OP_MAP[i].handler;
                        break;
                    }
                }
                if (handler) {
                    err = handler(&stack);
                } else {
                    CLEANUP_AND_RETURN(
                            SDDL2_INVALID_BYTECODE); // Unknown opcode
                }
                break;
            }

            // Unimplemented families
            case SDDL2_FAMILY_TYPE:
                if (opcode == SDDL2_OP_TYPE_FIXED_ARRAY) {
                    // Read array_count immediate (u32)
                    if (pc + 4 > bytecode_size) {
                        CLEANUP_AND_RETURN(
                                SDDL2_INVALID_BYTECODE); // Missing immediate
                    }
                    uint32_t array_count = ZL_readLE32(&bytecode[pc]);
                    pc += 4;
                    err = SDDL2_op_type_fixed_array(&stack, array_count);
                } else {
                    CLEANUP_AND_RETURN(
                            SDDL2_INVALID_BYTECODE); // Unknown opcode
                }
                break;

            case SDDL2_FAMILY_VAR:
            case SDDL2_FAMILY_EXPECT:
            case SDDL2_FAMILY_CALL:
                CLEANUP_AND_RETURN(
                        SDDL2_INVALID_BYTECODE); // Unimplemented family
        }

        // Check for errors
        if (err != SDDL2_OK) {
            CLEANUP_AND_RETURN(err);
        }
    }

    // Cleanup
    SDDL2_tag_registry_destroy(&registry);

    // Implicit halt: reaching the end of bytecode is treated as a successful
    // halt, even if no explicit halt instruction was encountered.
    // This makes simple programs more concise and follows the behavior of
    // high-level languages where functions can end without explicit return.
    return SDDL2_OK;
}

#undef CLEANUP_AND_RETURN
