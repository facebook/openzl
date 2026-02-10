// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "openzl/cpp/poly/StringView.hpp"

namespace openzl::sddl2 {

enum class Op {
    EXPECT,
    ASSIGN,
    SIZEOF,
    CONSUME,
    ASSUME,
    SEND,

    // Arithmetic Operators
    NEG,
    EQ,
    NE,
    GT,
    GE,
    LT,
    LE,
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,

    // Bitwise Operators
    BIT_AND,
    BIT_OR,
    BIT_XOR,
    BIT_NOT,

    // Logical Operators
    LOG_AND,
    LOG_OR,
    LOG_NOT,

    // Integer Numeric Types
    BYTE,
    U8,
    I8,
    U16LE,
    U16BE,
    I16LE,
    I16BE,
    U32LE,
    U32BE,
    I32LE,
    I32BE,
    U64LE,
    U64BE,
    I64LE,
    I64BE,

    // Float Numeric Types
    F8,
    F16LE,
    F16BE,
    F32LE,
    F32BE,
    F64LE,
    F64BE,
    BF8,
    BF16LE,
    BF16BE,
    BF32LE,
    BF32BE,
    BF64LE,
    BF64BE,

};

/// @returns a name string for an op.
/// (E.g., Op::ADD -> "ADD")
poly::string_view op_to_debug_str(Op op);

} // namespace openzl::sddl2
