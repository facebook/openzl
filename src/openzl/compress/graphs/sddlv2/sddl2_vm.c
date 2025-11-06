// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * OpenZL Execution Engine - VM Implementation
 * 
 * Implementation of non-performance-critical VM functions.
 * Performance-critical functions (push/pop) remain inlined in the header.
 */

#include "sddl2_vm.h"

/* ============================================================================
 * Stack Operations - Non-Critical Path
 * ========================================================================= */

void SDDL2_stack_init(openzl_stack* stack)
{
    stack->top = 0;
}

openzl_stack_error
SDDL2_stack_peek(const openzl_stack* stack, openzl_value* out)
{
    if (stack->top == 0) {
        return OPENZL_STACK_UNDERFLOW;
    }
    *out = stack->items[stack->top - 1];
    return OPENZL_STACK_OK;
}

size_t SDDL2_stack_depth(const openzl_stack* stack)
{
    return stack->top;
}

int SDDL2_stack_is_empty(const openzl_stack* stack)
{
    return stack->top == 0;
}

/* ============================================================================
 * Type Utilities
 * ========================================================================= */

size_t SDDL2_type_size(openzl_type_kind kind)
{
    switch (kind) {
    case OPENZL_TYPE_U8:
    case OPENZL_TYPE_I8:
        return 1;
    case OPENZL_TYPE_U16LE:
    case OPENZL_TYPE_U16BE:
    case OPENZL_TYPE_I16LE:
    case OPENZL_TYPE_I16BE:
        return 2;
    case OPENZL_TYPE_U32LE:
    case OPENZL_TYPE_U32BE:
    case OPENZL_TYPE_I32LE:
    case OPENZL_TYPE_I32BE:
    case OPENZL_TYPE_F32LE:
    case OPENZL_TYPE_F32BE:
        return 4;
    case OPENZL_TYPE_U64LE:
    case OPENZL_TYPE_U64BE:
    case OPENZL_TYPE_I64LE:
    case OPENZL_TYPE_I64BE:
    case OPENZL_TYPE_F64LE:
    case OPENZL_TYPE_F64BE:
        return 8;
    case OPENZL_TYPE_BYTES:
        return 1; // Raw bytes, unit size is 1 byte
    default:
        return 0; // Unknown type
    }
}
