// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/graphs/sddlv2/sddl2_interpreter.h"
#include <stdlib.h>
#include <string.h>
#include "openzl/shared/mem.h"

// Note: Opcodes are defined manually below, which is wrong.
// They should rather be extracted from sddl2_opcodes.def single source of truth

// Opcode families
#define FAMILY_CONTROL 0x0005
#define FAMILY_PUSH 0x0001
#define FAMILY_SEGMENT 0x000C

// Control opcodes
#define OP_HALT 0x0001

// Push opcodes
#define OP_PUSH_ZERO 0x0001
#define OP_PUSH_U32 0x0002
#define OP_PUSH_I32 0x0003
#define OP_PUSH_I64 0x0004

// Segment opcodes
#define OP_SEGMENT_CREATE_UNSPECIFIED 0x0001

SDDL2_error SDDL2_execute_bytecode(
        const void* bytecode_buffer,
        size_t bytecode_size,
        const void* input_data,
        size_t input_size,
        SDDL2_segment_list* output_segments)
{
    const char* bytecode = bytecode_buffer;

    // Validate inputs
    if (bytecode == NULL || output_segments == NULL) {
        return SDDL2_STACK_UNDERFLOW; // Reuse error code
    }

    if (bytecode_size % 4 != 0) {
        return SDDL2_STACK_UNDERFLOW; // Bytecode must be multiple of 4
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
    SDDL2_tag_registry_init(&registry);

    // Program counter (byte offset)
    size_t pc = 0;

    // Execution loop
    int halted = 0;
    while (pc < bytecode_size && !halted) {
        // Fetch instruction (32-bit word)
        if (pc + 4 > bytecode_size) {
            SDDL2_tag_registry_destroy(&registry);
            return SDDL2_STACK_UNDERFLOW; // Incomplete instruction
        }

        uint32_t instruction = ZL_readLE32(&bytecode[pc]);
        pc += 4;

        // Decode
        uint16_t family = instruction & 0xFFFF;
        uint16_t opcode = (instruction >> 16) & 0xFFFF;

        // Dispatch
        SDDL2_error err = SDDL2_OK;

        if (family == FAMILY_CONTROL) {
            if (opcode == OP_HALT) {
                halted = 1;
            } else {
                SDDL2_tag_registry_destroy(&registry);
                return SDDL2_STACK_UNDERFLOW; // Unknown opcode
            }
        } else if (family == FAMILY_PUSH) {
            if (opcode == OP_PUSH_ZERO) {
                err = SDDL2_stack_push(&stack, SDDL2_value_i64(0));
            } else if (opcode == OP_PUSH_U32) {
                if (pc + 4 > bytecode_size) {
                    SDDL2_tag_registry_destroy(&registry);
                    return SDDL2_STACK_UNDERFLOW; // Missing immediate
                }
                uint32_t value = ZL_readLE32(&bytecode[pc]);
                pc += 4;
                err = SDDL2_stack_push(&stack, SDDL2_value_i64((int64_t)value));
            } else if (opcode == OP_PUSH_I32) {
                if (pc + 4 > bytecode_size) {
                    SDDL2_tag_registry_destroy(&registry);
                    return SDDL2_STACK_UNDERFLOW; // Missing immediate
                }
                int32_t value = (int32_t)ZL_readLE32(&bytecode[pc]);
                pc += 4;
                err = SDDL2_stack_push(&stack, SDDL2_value_i64((int64_t)value));
            } else if (opcode == OP_PUSH_I64) {
                if (pc + 8 > bytecode_size) {
                    SDDL2_tag_registry_destroy(&registry);
                    return SDDL2_STACK_UNDERFLOW; // Missing immediate
                }
                int64_t value = (int64_t)ZL_readLE64(&bytecode[pc]);
                pc += 8;
                err = SDDL2_stack_push(&stack, SDDL2_value_i64(value));
            } else {
                SDDL2_tag_registry_destroy(&registry);
                return SDDL2_STACK_UNDERFLOW; // Unknown opcode
            }
        } else if (family == FAMILY_SEGMENT) {
            if (opcode == OP_SEGMENT_CREATE_UNSPECIFIED) {
                err = SDDL2_op_segment_create_unspecified(
                        &stack, &buffer, output_segments);
            } else {
                SDDL2_tag_registry_destroy(&registry);
                return SDDL2_STACK_UNDERFLOW; // Unknown opcode
            }
        } else {
            SDDL2_tag_registry_destroy(&registry);
            return SDDL2_STACK_UNDERFLOW; // Unknown family
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
        return SDDL2_STACK_UNDERFLOW; // Program didn't halt properly
    }

    return SDDL2_OK;
}
