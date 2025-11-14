// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * SDDL2 Disassembler - Instruction Introspection
 *
 * Provides debugging utilities for SDDL2 bytecode:
 * - Instruction name decoding
 * - Human-readable instruction formatting
 *
 * This module is intended for debugging and diagnostic purposes only.
 */

#ifndef SDDL2_DISASM_H
#define SDDL2_DISASM_H

#include <stdint.h>
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"

#if defined(__cplusplus)
extern "C" {
#endif

/**
 * Get human-readable name for an instruction.
 *
 * Decodes a family ID and opcode into an instruction mnemonic string.
 * Returns the same mnemonics used in SDDL2 assembly source code.
 *
 * @param family Family ID (bits 31-16 of instruction word)
 * @param opcode Opcode within family (bits 15-0 of instruction word)
 * @return Instruction name (e.g., "math.add", "push.zero", "halt")
 *         or "unknown.family" / "family.unknown" if not recognized
 *
 * Examples:
 *   SDDL2_instruction_name(0x0002, 0x0001) → "math.add"
 *   SDDL2_instruction_name(0x0005, 0x0001) → "halt"
 *   SDDL2_instruction_name(0x0001, 0x0080) → "push.current_pos"
 */
const char* SDDL2_instruction_name(uint16_t family, uint16_t opcode);

/**
 * Log detailed diagnostics for expect_true validation failure.
 *
 * Outputs comprehensive error information including the failed value
 * and current stack state to aid in debugging validation failures.
 *
 * @param value The value that failed validation (expected non-zero, got 0)
 * @param stack The stack after the value was popped (for context)
 */
void SDDL2_log_expect_true_failure(int64_t value, const SDDL2_Stack* stack);

#if defined(__cplusplus)
}
#endif

#endif // SDDL2_DISASM_H
