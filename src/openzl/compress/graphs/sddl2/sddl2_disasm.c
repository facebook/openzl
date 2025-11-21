// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * SDDL2 Disassembler - Implementation
 *
 * Provides instruction name decoding for debugging purposes.
 * The implementation is auto-generated from sddl2_opcodes.def.
 */

#include "sddl2_disasm.h"

// Include the auto-generated implementation
#include "openzl/compress/graphs/sddl2/sddl2_disasm_generated.h"

const char* SDDL2_instruction_name(uint16_t family, uint16_t opcode)
{
    return SDDL2_instruction_name_impl(family, opcode);
}
