// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include "openzl/cpp/poly/StringView.hpp"

namespace openzl::sddl2 {

enum class Op {
    EXPECT,
    ASSIGN,
    SIZEOF,
    CONSUME,
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
};

/// @returns a name string for an op.
/// (E.g., Op::ADD -> "ADD")
poly::string_view op_to_debug_str(Op op);

} // namespace openzl::sddl2
