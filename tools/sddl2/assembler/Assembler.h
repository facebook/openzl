// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>
#include <stdexcept>
#include <vector>

#include "openzl/cpp/poly/StringView.hpp"

namespace openzl::sddl2 {

// ── Error type ──────────────────────────────────────────────────────────────

class AssemblerError : public std::runtime_error {
   public:
    using std::runtime_error::runtime_error;
};

// ── Assembler ───────────────────────────────────────────────────────────────

/**
 * SDDL2 assembler: translates SDDL2 assembly text to VM bytecode.
 *
 * The instruction tables are built-in (mirroring sddl2_opcodes.def).
 * All encoding follows the little-endian 32-bit instruction word format
 * described in the Bytecode Specification.
 *
 * Usage:
 * ```
 *   Assembler as;
 *   auto bytecode = as.assemble("push.i32 42\nhalt");
 * ```
 */
class Assembler {
   public:
    Assembler() = default;

    /**
     * Assemble a complete source string to bytecode.
     *
     * @param source  Assembly text (may contain comments, blank lines, etc.)
     * @returns       Bytecode as a vector of bytes.
     * @throws AssemblerError on any syntax or encoding error.
     */
    std::vector<uint8_t> assemble(poly::string_view source) const;
};

} // namespace openzl::sddl2
