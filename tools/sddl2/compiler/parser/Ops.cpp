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

    // Integer Numeric Types
    { Op::BYTE, "BYTE" },
    { Op::U8, "U8" },
    { Op::I8, "I8" },
    { Op::U16LE, "U16LE" },
    { Op::U16BE, "U16BE" },
    { Op::I16LE, "I16LE" },
    { Op::I16BE, "I16BE" },
    { Op::U32LE, "U32LE" },
    { Op::U32BE, "U32BE" },
    { Op::I32LE, "I32LE" },
    { Op::I32BE, "I32BE" },
    { Op::U64LE, "U64LE" },
    { Op::U64BE, "U64BE" },
    { Op::I64LE, "I64LE" },
    { Op::I64BE, "I64BE" },

    // Float Numeric Types
    { Op::F8, "F8" },
    { Op::F16LE, "F16LE" },
    { Op::F16BE, "F16BE" },
    { Op::F32LE, "F32LE" },
    { Op::F32BE, "F32BE" },
    { Op::F64LE, "F64LE" },
    { Op::F64BE, "F64BE" },
    { Op::BF8, "BF8" },
    { Op::BF16LE, "BF16LE" },
    { Op::BF16BE, "BF16BE" },
    { Op::BF32LE, "BF32LE" },
    { Op::BF32BE, "BF32BE" },
    { Op::BF64LE, "BF64LE" },
    { Op::BF64BE, "BF64BE" },

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
