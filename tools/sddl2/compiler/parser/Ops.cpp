// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <map>
#include <stdexcept>

#include "tools/sddl2/compiler/parser/Ops.h"

namespace openzl::sddl2 {

static const std::map<Op, poly::string_view> ops_to_debug_strs{
    { Op::EXPECT, "EXPECT" },
    { Op::ASSIGN, "ASSIGN" },
    { Op::CONSUME, "CONSUME" },
    { Op::ASSUME, "ASSUME" },
    { Op::SEND, "SEND" },
    { Op::SIZEOF, "SIZEOF" },

    // Arithmetic Operators
    { Op::NEG, "NEG" },
    { Op::EQ, "EQ" },
    { Op::NE, "NE" },
    { Op::GT, "GT" },
    { Op::GE, "GE" },
    { Op::LT, "LT" },
    { Op::LE, "LE" },
    { Op::ADD, "ADD" },
    { Op::SUB, "SUB" },
    { Op::MUL, "MUL" },
    { Op::DIV, "DIV" },
    { Op::MOD, "MOD" },

    // Bitwise Operators
    { Op::BIT_AND, "BIT_AND" },
    { Op::BIT_OR, "BIT_OR" },
    { Op::BIT_XOR, "BIT_XOR" },
    { Op::BIT_NOT, "BIT_NOT" },

    // Logical Operators
    { Op::LOG_AND, "LOG_AND" },
    { Op::LOG_OR, "LOG_OR" },
    { Op::LOG_NOT, "LOG_NOT" },
};

poly::string_view op_to_debug_str(Op op)
{
    try {
        return ops_to_debug_strs.at(op);
    } catch (const std::out_of_range&) {
        static const poly::string_view unknown{ "UNKNOWN???" };
        return unknown;
    }
}

} // namespace openzl::sddl2
