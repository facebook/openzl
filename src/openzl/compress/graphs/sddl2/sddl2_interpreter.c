// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/graphs/sddl2/sddl2_interpreter.h"
#include "openzl/common/logging.h"
#include "openzl/compress/graphs/sddl2/sddl2_disasm.h"
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
    SDDL2_Type_kind kind;
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
 * All MATH, CMP, LOGIC, and STACK operations share this signature.
 * Operations that don't use tracing simply ignore the trace and pc parameters.
 */
typedef SDDL2_Error (
        *SDDL2_Stack_op_fn)(SDDL2_Stack*, SDDL2_Trace_buffer*, size_t pc);

/**
 * Common dispatch table entry type for stack operations.
 * Used by MATH, CMP, and STACK operation lookup tables.
 */
typedef struct {
    uint16_t opcode;
    SDDL2_Stack_op_fn handler;
} SDDL2_Stack_op_entry;

/**
 * Lookup table for MATH family operations.
 */
static const SDDL2_Stack_op_entry MATH_OP_MAP[] = {
    { SDDL2_OP_MATH_ADD, SDDL2_op_add }, { SDDL2_OP_MATH_SUB, SDDL2_op_sub },
    { SDDL2_OP_MATH_MUL, SDDL2_op_mul }, { SDDL2_OP_MATH_DIV, SDDL2_op_div },
    { SDDL2_OP_MATH_MOD, SDDL2_op_mod }, { SDDL2_OP_MATH_ABS, SDDL2_op_abs },
    { SDDL2_OP_MATH_NEG, SDDL2_op_neg },
};

#define MATH_OP_MAP_SIZE (sizeof(MATH_OP_MAP) / sizeof(MATH_OP_MAP[0]))

/**
 * Lookup table for CMP family operations.
 */
static const SDDL2_Stack_op_entry CMP_OP_MAP[] = {
    { SDDL2_OP_CMP_EQ, SDDL2_op_eq }, { SDDL2_OP_CMP_NE, SDDL2_op_ne },
    { SDDL2_OP_CMP_LT, SDDL2_op_lt }, { SDDL2_OP_CMP_LE, SDDL2_op_le },
    { SDDL2_OP_CMP_GT, SDDL2_op_gt }, { SDDL2_OP_CMP_GE, SDDL2_op_ge },
};

#define CMP_OP_MAP_SIZE (sizeof(CMP_OP_MAP) / sizeof(CMP_OP_MAP[0]))

/**
 * Lookup table for LOGIC family operations.
 */
static const SDDL2_Stack_op_entry LOGIC_OP_MAP[] = {
    { SDDL2_OP_LOGIC_AND, SDDL2_op_and },
    { SDDL2_OP_LOGIC_OR, SDDL2_op_or },
    { SDDL2_OP_LOGIC_XOR, SDDL2_op_xor },
    { SDDL2_OP_LOGIC_NOT, SDDL2_op_not },
};

#define LOGIC_OP_MAP_SIZE (sizeof(LOGIC_OP_MAP) / sizeof(LOGIC_OP_MAP[0]))

/**
 * Lookup table for STACK family operations.
 */
static const SDDL2_Stack_op_entry STACK_OP_MAP[] = {
    { SDDL2_OP_STACK_DROP, SDDL2_op_drop },
    { SDDL2_OP_STACK_DROP_IF, SDDL2_op_stack_drop_if },
    { SDDL2_OP_STACK_DUP, SDDL2_op_dup },
    { SDDL2_OP_STACK_SWAP, SDDL2_op_swap },
};

#define STACK_OP_MAP_SIZE (sizeof(STACK_OP_MAP) / sizeof(STACK_OP_MAP[0]))

/**
 * Function pointer type for LOAD operations.
 * LOAD operations require both stack and input buffer access.
 */
typedef SDDL2_Error (
        *SDDL2_Load_op_fn)(SDDL2_Stack*, const SDDL2_Input_cursor*);

/**
 * Dispatch table entry type for LOAD operations.
 */
typedef struct {
    uint16_t opcode;
    SDDL2_Load_op_fn handler;
} SDDL2_Load_op_entry;

/**
 * Lookup table for LOAD family operations.
 */
static const SDDL2_Load_op_entry LOAD_OP_MAP[] = {
    { SDDL2_OP_LOAD_U8, SDDL2_op_load_u8 },
    { SDDL2_OP_LOAD_I8, SDDL2_op_load_i8 },
    { SDDL2_OP_LOAD_U16LE, SDDL2_op_load_u16le },
    { SDDL2_OP_LOAD_U16BE, SDDL2_op_load_u16be },
    { SDDL2_OP_LOAD_I16LE, SDDL2_op_load_i16le },
    { SDDL2_OP_LOAD_I16BE, SDDL2_op_load_i16be },
    { SDDL2_OP_LOAD_U32LE, SDDL2_op_load_u32le },
    { SDDL2_OP_LOAD_U32BE, SDDL2_op_load_u32be },
    { SDDL2_OP_LOAD_I32LE, SDDL2_op_load_i32le },
    { SDDL2_OP_LOAD_I32BE, SDDL2_op_load_i32be },
    { SDDL2_OP_LOAD_I64LE, SDDL2_op_load_i64le },
    { SDDL2_OP_LOAD_I64BE, SDDL2_op_load_i64be },
};

#define LOAD_OP_MAP_SIZE (sizeof(LOAD_OP_MAP) / sizeof(LOAD_OP_MAP[0]))

/* ============================================================================
 * Immediate Value Reading Helpers
 * ========================================================================= */

/**
 * Read a 32-bit unsigned immediate value from bytecode.
 *
 * @param bytecode Bytecode buffer
 * @param bytecode_size Total bytecode size
 * @param pc Program counter (will be advanced by 4 on success)
 * @param out_value Output parameter for the read value
 * @return SDDL2_OK on success, SDDL2_INVALID_BYTECODE if insufficient bytes
 */
static inline SDDL2_Error read_u32_immediate(
        const char* bytecode,
        size_t bytecode_size,
        size_t* pc,
        uint32_t* out_value)
{
    if (*pc + 4 > bytecode_size) {
        return SDDL2_INVALID_BYTECODE;
    }
    *out_value = ZL_readLE32(&bytecode[*pc]);
    *pc += 4;
    return SDDL2_OK;
}

/**
 * Read a 32-bit signed immediate value from bytecode.
 *
 * @param bytecode Bytecode buffer
 * @param bytecode_size Total bytecode size
 * @param pc Program counter (will be advanced by 4 on success)
 * @param out_value Output parameter for the read value
 * @return SDDL2_OK on success, SDDL2_INVALID_BYTECODE if insufficient bytes
 */
static inline SDDL2_Error read_i32_immediate(
        const char* bytecode,
        size_t bytecode_size,
        size_t* pc,
        int32_t* out_value)
{
    if (*pc + 4 > bytecode_size) {
        return SDDL2_INVALID_BYTECODE;
    }
    *out_value = (int32_t)ZL_readLE32(&bytecode[*pc]);
    *pc += 4;
    return SDDL2_OK;
}

/**
 * Read a 64-bit signed immediate value from bytecode.
 *
 * @param bytecode Bytecode buffer
 * @param bytecode_size Total bytecode size
 * @param pc Program counter (will be advanced by 8 on success)
 * @param out_value Output parameter for the read value
 * @return SDDL2_OK on success, SDDL2_INVALID_BYTECODE if insufficient bytes
 */
static inline SDDL2_Error read_i64_immediate(
        const char* bytecode,
        size_t bytecode_size,
        size_t* pc,
        int64_t* out_value)
{
    if (*pc + 8 > bytecode_size) {
        return SDDL2_INVALID_BYTECODE;
    }
    *out_value = (int64_t)ZL_readLE64(&bytecode[*pc]);
    *pc += 8;
    return SDDL2_OK;
}

/* ============================================================================
 * Dispatch Helpers
 * ========================================================================= */

/**
 * Generic lookup helper for operation dispatch tables.
 * Searches a dispatch table for a matching opcode and returns the handler.
 *
 * @param opcode The opcode to search for
 * @param map The dispatch table to search
 * @param map_size Number of entries in the dispatch table
 * @return Handler function pointer if found, NULL otherwise
 *
 * This is a static inline function instead of a macro to ensure standard C11
 * compliance (avoiding GCC statement expressions extension).
 */
static inline SDDL2_Stack_op_fn find_stack_op_handler(
        uint16_t opcode,
        const SDDL2_Stack_op_entry* map,
        size_t map_size)
{
    for (size_t i = 0; i < map_size; i++) {
        if (opcode == map[i].opcode) {
            return map[i].handler;
        }
    }
    return NULL;
}

/**
 * Lookup helper for LOAD operation dispatch table.
 * Searches the dispatch table for a matching opcode and returns the handler.
 *
 * @param opcode The opcode to search for
 * @param map The dispatch table to search
 * @param map_size Number of entries in the dispatch table
 * @return Handler function pointer if found, NULL otherwise
 */
static inline SDDL2_Load_op_fn find_load_op_handler(
        uint16_t opcode,
        const SDDL2_Load_op_entry* map,
        size_t map_size)
{
    for (size_t i = 0; i < map_size; i++) {
        if (opcode == map[i].opcode) {
            return map[i].handler;
        }
    }
    return NULL;
}

/* ============================================================================
 * Family Handler Functions
 * ========================================================================= */

/**
 * Handle all PUSH family operations.
 * The PUSH family includes immediate values, constants, buffer queries,
 * and type push operations.
 *
 * @param opcode The specific PUSH opcode to execute
 * @param bytecode Bytecode buffer (for reading immediate values)
 * @param bytecode_size Total bytecode size
 * @param pc Program counter pointer (advanced when reading immediates)
 * @param stack Stack to push values onto
 * @param buffer Input buffer (for position/remaining queries)
 * @return SDDL2_OK on success, error code on failure
 */
static SDDL2_Error handle_push_family(
        uint16_t opcode,
        const char* bytecode,
        size_t bytecode_size,
        size_t* pc,
        SDDL2_Stack* stack,
        const SDDL2_Input_cursor* buffer)
{
    SDDL2_Error err = SDDL2_OK;

    if (opcode == SDDL2_OP_PUSH_ZERO) {
        err = SDDL2_Stack_push(stack, SDDL2_Value_i64(0));
    } else if (opcode == SDDL2_OP_PUSH_U32) {
        uint32_t value;
        if ((err = read_u32_immediate(bytecode, bytecode_size, pc, &value))
            != SDDL2_OK) {
            return err;
        }
        err = SDDL2_Stack_push(stack, SDDL2_Value_i64((int64_t)value));
    } else if (opcode == SDDL2_OP_PUSH_I32) {
        int32_t value;
        if ((err = read_i32_immediate(bytecode, bytecode_size, pc, &value))
            != SDDL2_OK) {
            return err;
        }
        err = SDDL2_Stack_push(stack, SDDL2_Value_i64((int64_t)value));
    } else if (opcode == SDDL2_OP_PUSH_I64) {
        int64_t value;
        if ((err = read_i64_immediate(bytecode, bytecode_size, pc, &value))
            != SDDL2_OK) {
            return err;
        }
        err = SDDL2_Stack_push(stack, SDDL2_Value_i64(value));
    } else if (opcode == SDDL2_OP_PUSH_TAG) {
        uint32_t tag;
        if ((err = read_u32_immediate(bytecode, bytecode_size, pc, &tag))
            != SDDL2_OK) {
            return err;
        }
        err = SDDL2_Stack_push(stack, SDDL2_Value_tag(tag));
    } else if (opcode == SDDL2_OP_PUSH_CURRENT_POS) {
        err = SDDL2_op_current_pos(stack, buffer);
    } else if (opcode == SDDL2_OP_PUSH_REMAINING) {
        err = SDDL2_op_remaining(stack, buffer);
    } else if (opcode == SDDL2_OP_PUSH_STACK_DEPTH) {
        err = SDDL2_op_push_stack_depth(stack);
    } else {
        // Handle all push.type opcodes via lookup table
        int found = 0;
        for (size_t i = 0; i < PUSH_TYPE_MAP_SIZE; i++) {
            if (opcode == PUSH_TYPE_MAP[i].opcode) {
                SDDL2_Type type = { .kind = PUSH_TYPE_MAP[i].kind, .width = 1 };
                err   = SDDL2_Stack_push(stack, SDDL2_Value_type(type));
                found = 1;
                break;
            }
        }
        if (!found) {
            return SDDL2_INVALID_BYTECODE;
        }
    }

    return err;
}

/**
 * Cleanup helper for SDDL2_execute_bytecode.
 * Destroys the tag registry, trace buffer, and returns the specified error
 * code.
 *
 * This macro is function-scoped and undefined after SDDL2_execute_bytecode.
 */
#define CLEANUP_AND_RETURN(error_code)         \
    do {                                       \
        SDDL2_Tag_registry_destroy(&registry); \
        SDDL2_Trace_buffer_destroy(&trace);    \
        return (error_code);                   \
    } while (0)

/**
 * Dispatch helper macro for stack operation families (MATH, CMP, LOGIC, STACK).
 * Looks up the handler in the specified dispatch table and executes it,
 * passing the trace buffer and program counter for trace recording.
 * Returns SDDL2_INVALID_BYTECODE if no handler is found.
 *
 * This macro is function-scoped and undefined after SDDL2_execute_bytecode.
 */
#define DISPATCH_STACK_OP(map, map_size)                              \
    do {                                                              \
        SDDL2_Stack_op_fn handler =                                   \
                find_stack_op_handler(opcode, map, map_size);         \
        if (handler) {                                                \
            err = handler(&stack, &trace, pc_before);                 \
        } else {                                                      \
            CLEANUP_AND_RETURN(SDDL2_INVALID_BYTECODE); /* Unknown */ \
        }                                                             \
    } while (0)

/**
 * Dispatch helper macro for LOAD operation family.
 * Looks up the handler in the LOAD dispatch table and executes it,
 * or returns SDDL2_INVALID_BYTECODE if no handler is found.
 *
 * This macro is function-scoped and undefined after SDDL2_execute_bytecode.
 */
#define DISPATCH_LOAD_OP(map, map_size)                               \
    do {                                                              \
        SDDL2_Load_op_fn handler =                                    \
                find_load_op_handler(opcode, map, map_size);          \
        if (handler) {                                                \
            err = handler(&stack, &buffer);                           \
        } else {                                                      \
            CLEANUP_AND_RETURN(SDDL2_INVALID_BYTECODE); /* Unknown */ \
        }                                                             \
    } while (0)

SDDL2_Error SDDL2_execute_bytecode(
        const void* bytecode_buffer,
        size_t bytecode_size,
        const void* input_data,
        size_t input_size,
        SDDL2_Segment_list* output_segments)
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
    SDDL2_Stack stack;
    SDDL2_Value stack_storage[256];
    stack.items    = stack_storage;
    stack.capacity = 256;
    SDDL2_Stack_init(&stack);

    SDDL2_Input_cursor buffer;
    SDDL2_Input_cursor_init(&buffer, input_data, input_size);

    SDDL2_Tag_registry registry;
    // Use same allocator as output_segments (arena in production, NULL in
    // tests)
    SDDL2_Tag_registry_init(
            &registry, output_segments->alloc_fn, output_segments->alloc_ctx);

    SDDL2_Trace_buffer trace;
    // Use same allocator as output_segments
    SDDL2_Trace_buffer_init(
            &trace, output_segments->alloc_fn, output_segments->alloc_ctx);

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
        size_t pc_before     = pc;
        pc += 4;

        // Decode instruction word
        // Bits 31-16: Family ID
        // Bits 15-0:  Opcode within family
        uint16_t family = (uint16_t)((instruction >> 16) & 0xFFFF);
        uint16_t opcode = (uint16_t)(instruction & 0xFFFF);

        ZL_DLOG(SEQ,
                "[SDDL2] PC=%zu: %s (0x%08x) stack_depth=%zu",
                pc_before,
                SDDL2_instruction_name(family, opcode),
                instruction,
                SDDL2_Stack_depth(&stack));

        // Dispatch
        SDDL2_Error err = SDDL2_OK;

        switch (family) {
            case SDDL2_FAMILY_CONTROL:
                if (opcode == SDDL2_OP_CONTROL_HALT) {
                    halted = 1;
                } else if (opcode == SDDL2_OP_CONTROL_EXPECT_TRUE) {
                    err = SDDL2_op_expect_true(&stack, &trace);
                } else if (opcode == SDDL2_OP_CONTROL_TRACE_START) {
                    SDDL2_Trace_buffer_start(&trace);
                } else {
                    CLEANUP_AND_RETURN(
                            SDDL2_INVALID_BYTECODE); // Unknown opcode
                }
                break;

            case SDDL2_FAMILY_PUSH:
                err = handle_push_family(
                        opcode, bytecode, bytecode_size, &pc, &stack, &buffer);
                if (err != SDDL2_OK) {
                    CLEANUP_AND_RETURN(err);
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

            // Stack operation families (uniform dispatch)
            case SDDL2_FAMILY_MATH:
                DISPATCH_STACK_OP(MATH_OP_MAP, MATH_OP_MAP_SIZE);
                break;

            case SDDL2_FAMILY_CMP:
                DISPATCH_STACK_OP(CMP_OP_MAP, CMP_OP_MAP_SIZE);
                break;

            case SDDL2_FAMILY_LOGIC:
                DISPATCH_STACK_OP(LOGIC_OP_MAP, LOGIC_OP_MAP_SIZE);
                break;

            case SDDL2_FAMILY_STACK:
                DISPATCH_STACK_OP(STACK_OP_MAP, STACK_OP_MAP_SIZE);
                break;

            case SDDL2_FAMILY_TYPE:
                if (opcode == SDDL2_OP_TYPE_FIXED_ARRAY) {
                    err = SDDL2_op_type_fixed_array(&stack);
                } else if (opcode == SDDL2_OP_TYPE_STRUCTURE) {
                    err = SDDL2_op_type_structure(
                            &stack,
                            output_segments->alloc_fn,
                            output_segments->alloc_ctx);
                } else if (opcode == SDDL2_OP_TYPE_SIZEOF) {
                    err = SDDL2_op_type_sizeof(&stack);
                } else {
                    CLEANUP_AND_RETURN(
                            SDDL2_INVALID_BYTECODE); // Unknown opcode
                }
                break;

            case SDDL2_FAMILY_LOAD:
                DISPATCH_LOAD_OP(LOAD_OP_MAP, LOAD_OP_MAP_SIZE);
                break;

            // Note: expect_true is part of CONTROL family (ID 0x000A not
            // generated for empty EXPECT family)

            // Unimplemented families
            case SDDL2_FAMILY_VAR:
            case SDDL2_FAMILY_CALL:
                CLEANUP_AND_RETURN(SDDL2_INVALID_BYTECODE);
        }

        // Check for errors
        if (err != SDDL2_OK) {
            CLEANUP_AND_RETURN(err);
        }
    }

    // Cleanup
    SDDL2_Tag_registry_destroy(&registry);
    SDDL2_Trace_buffer_destroy(&trace);

    // Implicit halt: reaching the end of bytecode is treated as a successful
    // halt, even if no explicit halt instruction was encountered.
    // This makes simple programs more concise and follows the behavior of
    // high-level languages where functions can end without explicit return.
    return SDDL2_OK;
}

#undef CLEANUP_AND_RETURN
#undef DISPATCH_STACK_OP
#undef DISPATCH_LOAD_OP
