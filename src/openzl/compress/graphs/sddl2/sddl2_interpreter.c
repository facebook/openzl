// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/graphs/sddl2/sddl2_interpreter.h"
#include "openzl/common/logging.h"
#include "openzl/compress/graphs/sddl2/sddl2_disasm.h"
#include "openzl/compress/graphs/sddl2/sddl2_opcodes.h"
#include "openzl/shared/mem.h"

/* ============================================================================
 * X-Macro Definitions for Opcode Families
 * ========================================================================= */

/**
 * X-macro pattern for opcode dispatch.
 * This approach generates switch cases at compile time, guaranteeing no runtime
 * array scans and allowing the same authoritative list to be reused for
 * multiple purposes (dispatch, name generation, etc.).
 *
 * Pattern inspired by: https://news.ycombinator.com/item?id=43472143
 */

// clang-format off
#define FOR_EACH_LOAD_OP(_func)                   \
    _func(SDDL2_OP_LOAD_U8, SDDL2_op_load_u8)     \
    _func(SDDL2_OP_LOAD_I8, SDDL2_op_load_i8)     \
    _func(SDDL2_OP_LOAD_U16LE, SDDL2_op_load_u16le) \
    _func(SDDL2_OP_LOAD_U16BE, SDDL2_op_load_u16be) \
    _func(SDDL2_OP_LOAD_I16LE, SDDL2_op_load_i16le) \
    _func(SDDL2_OP_LOAD_I16BE, SDDL2_op_load_i16be) \
    _func(SDDL2_OP_LOAD_U32LE, SDDL2_op_load_u32le) \
    _func(SDDL2_OP_LOAD_U32BE, SDDL2_op_load_u32be) \
    _func(SDDL2_OP_LOAD_I32LE, SDDL2_op_load_i32le) \
    _func(SDDL2_OP_LOAD_I32BE, SDDL2_op_load_i32be) \
    _func(SDDL2_OP_LOAD_I64LE, SDDL2_op_load_i64le) \
    _func(SDDL2_OP_LOAD_I64BE, SDDL2_op_load_i64be)

#define FOR_EACH_MATH_OP(_func)              \
    _func(SDDL2_OP_MATH_ADD, SDDL2_op_add)   \
    _func(SDDL2_OP_MATH_SUB, SDDL2_op_sub)   \
    _func(SDDL2_OP_MATH_MUL, SDDL2_op_mul)   \
    _func(SDDL2_OP_MATH_DIV, SDDL2_op_div)   \
    _func(SDDL2_OP_MATH_MOD, SDDL2_op_mod)   \
    _func(SDDL2_OP_MATH_ABS, SDDL2_op_abs)   \
    _func(SDDL2_OP_MATH_NEG, SDDL2_op_neg)

#define FOR_EACH_CMP_OP(_func)             \
    _func(SDDL2_OP_CMP_EQ, SDDL2_op_eq)    \
    _func(SDDL2_OP_CMP_NE, SDDL2_op_ne)    \
    _func(SDDL2_OP_CMP_LT, SDDL2_op_lt)    \
    _func(SDDL2_OP_CMP_LE, SDDL2_op_le)    \
    _func(SDDL2_OP_CMP_GT, SDDL2_op_gt)    \
    _func(SDDL2_OP_CMP_GE, SDDL2_op_ge)

#define FOR_EACH_LOGIC_OP(_func)             \
    _func(SDDL2_OP_LOGIC_AND, SDDL2_op_and)  \
    _func(SDDL2_OP_LOGIC_OR, SDDL2_op_or)    \
    _func(SDDL2_OP_LOGIC_XOR, SDDL2_op_xor)  \
    _func(SDDL2_OP_LOGIC_NOT, SDDL2_op_not)

#define FOR_EACH_STACK_OP(_func)                       \
    _func(SDDL2_OP_STACK_DROP, SDDL2_op_drop)          \
    _func(SDDL2_OP_STACK_DROP_IF, SDDL2_op_stack_drop_if) \
    _func(SDDL2_OP_STACK_DUP, SDDL2_op_dup)            \
    _func(SDDL2_OP_STACK_SWAP, SDDL2_op_swap)

#define FOR_EACH_PUSH_TYPE_OP(_func)                         \
    _func(SDDL2_OP_PUSH_TYPE_BYTES, SDDL2_TYPE_BYTES)        \
    _func(SDDL2_OP_PUSH_TYPE_U8, SDDL2_TYPE_U8)              \
    _func(SDDL2_OP_PUSH_TYPE_I8, SDDL2_TYPE_I8)              \
    _func(SDDL2_OP_PUSH_TYPE_U16LE, SDDL2_TYPE_U16LE)        \
    _func(SDDL2_OP_PUSH_TYPE_U16BE, SDDL2_TYPE_U16BE)        \
    _func(SDDL2_OP_PUSH_TYPE_I16LE, SDDL2_TYPE_I16LE)        \
    _func(SDDL2_OP_PUSH_TYPE_I16BE, SDDL2_TYPE_I16BE)        \
    _func(SDDL2_OP_PUSH_TYPE_U32LE, SDDL2_TYPE_U32LE)        \
    _func(SDDL2_OP_PUSH_TYPE_U32BE, SDDL2_TYPE_U32BE)        \
    _func(SDDL2_OP_PUSH_TYPE_I32LE, SDDL2_TYPE_I32LE)        \
    _func(SDDL2_OP_PUSH_TYPE_I32BE, SDDL2_TYPE_I32BE)        \
    _func(SDDL2_OP_PUSH_TYPE_U64LE, SDDL2_TYPE_U64LE)        \
    _func(SDDL2_OP_PUSH_TYPE_U64BE, SDDL2_TYPE_U64BE)        \
    _func(SDDL2_OP_PUSH_TYPE_I64LE, SDDL2_TYPE_I64LE)        \
    _func(SDDL2_OP_PUSH_TYPE_I64BE, SDDL2_TYPE_I64BE)        \
    _func(SDDL2_OP_PUSH_TYPE_F8, SDDL2_TYPE_F8)              \
    _func(SDDL2_OP_PUSH_TYPE_F16LE, SDDL2_TYPE_F16LE)        \
    _func(SDDL2_OP_PUSH_TYPE_F16BE, SDDL2_TYPE_F16BE)        \
    _func(SDDL2_OP_PUSH_TYPE_BF16LE, SDDL2_TYPE_BF16LE)      \
    _func(SDDL2_OP_PUSH_TYPE_BF16BE, SDDL2_TYPE_BF16BE)      \
    _func(SDDL2_OP_PUSH_TYPE_F32LE, SDDL2_TYPE_F32LE)        \
    _func(SDDL2_OP_PUSH_TYPE_F32BE, SDDL2_TYPE_F32BE)        \
    _func(SDDL2_OP_PUSH_TYPE_F64LE, SDDL2_TYPE_F64LE)        \
    _func(SDDL2_OP_PUSH_TYPE_F64BE, SDDL2_TYPE_F64BE)
// clang-format on

/* ============================================================================
 * X-Macro Dispatch Helpers
 * ========================================================================= */

/**
 * Helper macro to generate a switch case for stack operations.
 * Used with FOR_EACH_*_OP macros to generate compile-time dispatch.
 */
#define EMIT_STACK_OP_CASE(_opcode, _handler)      \
    case _opcode:                                  \
        err = _handler(&stack, &trace, pc_before); \
        break;

/**
 * Helper macro to generate a switch case for LOAD operations.
 * Used with FOR_EACH_LOAD_OP macro to generate compile-time dispatch.
 */
#define EMIT_LOAD_OP_CASE(_opcode, _handler) \
    case _opcode:                            \
        err = _handler(&stack, &buffer);     \
        break;

/**
 * Helper macro to generate a switch case for PUSH_TYPE operations.
 * Used with FOR_EACH_PUSH_TYPE_OP macro to generate compile-time dispatch.
 */
#define EMIT_PUSH_TYPE_CASE(_opcode, _type_kind)                           \
    case _opcode: {                                                        \
        SDDL2_Type type = { .kind = _type_kind, .width = 1 };              \
        err             = SDDL2_Stack_push(stack, SDDL2_Value_type(type)); \
        found           = 1;                                               \
        break;                                                             \
    }

/**
 * Dispatch macro for opcode families using X-macro switch.
 * Generates a switch statement with cases for all opcodes in the family.
 * Returns SDDL2_INVALID_BYTECODE for unknown opcodes.
 */
#define DISPATCH_OP_FAMILY(_family_for_each, _emit_case)          \
    do {                                                          \
        switch (opcode) {                                         \
            _family_for_each(_emit_case) default                  \
                    : CLEANUP_AND_RETURN(SDDL2_INVALID_BYTECODE); \
        }                                                         \
    } while (0)

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
 * PUSH Family Handler
 * ========================================================================= */

/**
 * Handle all PUSH family operations.
 *
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
        // Handle all push.type opcodes via X-macro dispatch
        int found = 0;
        switch (opcode) {
            FOR_EACH_PUSH_TYPE_OP(EMIT_PUSH_TYPE_CASE)
            default:
                return SDDL2_INVALID_BYTECODE;
        }
        (void)found;
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

            // Stack operation families (X-macro dispatch)
            case SDDL2_FAMILY_MATH:
                DISPATCH_OP_FAMILY(FOR_EACH_MATH_OP, EMIT_STACK_OP_CASE);
                break;

            case SDDL2_FAMILY_CMP:
                DISPATCH_OP_FAMILY(FOR_EACH_CMP_OP, EMIT_STACK_OP_CASE);
                break;

            case SDDL2_FAMILY_LOGIC:
                DISPATCH_OP_FAMILY(FOR_EACH_LOGIC_OP, EMIT_STACK_OP_CASE);
                break;

            case SDDL2_FAMILY_STACK:
                DISPATCH_OP_FAMILY(FOR_EACH_STACK_OP, EMIT_STACK_OP_CASE);
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
                DISPATCH_OP_FAMILY(FOR_EACH_LOAD_OP, EMIT_LOAD_OP_CASE);
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
