// Copyright (c) Meta Platforms, Inc. and affiliates.

// AUTO-GENERATED FILE - DO NOT EDIT MANUALLY
//
// Generated from: src/openzl/compress/graphs/sddl2/sddl2_opcodes.def
// Generated at: 2026-02-13 16:24:43 UTC
// Generator: generate_opcodes.py
//
// To regenerate: python3 generate_opcodes.py

#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace openzl {
namespace sddl2 {
namespace opcodes {

enum class Family : uint16_t {
    PUSH    = 0x0001,
    MATH    = 0x0002,
    CMP     = 0x0003,
    LOGIC   = 0x0004,
    CONTROL = 0x0005,
    LOAD    = 0x0006,
    STACK   = 0x0007,
    TYPE    = 0x0008,
    VAR     = 0x0009,
    CALL    = 0x000B,
    SEGMENT = 0x000C,
};

enum class ParamType : uint8_t {
    U32,
    I32,
    I64,
};

struct Instruction {
    Family family;
    uint16_t opcode;
    std::vector<ParamType> params;
};

std::optional<Instruction> getInstruction(const std::string& mnemonic);

} // namespace opcodes
} // namespace sddl2
} // namespace openzl
