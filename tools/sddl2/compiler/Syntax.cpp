// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl2/compiler/Syntax.h"

#include <stdexcept>

#include "tools/sddl2/compiler/Exception.h"

namespace openzl::sddl2 {

static const std::map<ListType, poly::string_view> list_types_to_debug_strs{
    { ListType::PAREN, "PAREN" },
    { ListType::SQUARE, "SQUARE" },
    { ListType::CURLY, "CURLY" },
};

poly::string_view list_type_to_debug_str(ListType list_type)
{
    try {
        return list_types_to_debug_strs.at(list_type);
    } catch (const std::out_of_range&) {
        throw InvariantViolation("Lookup failed in list_type_to_debug_str()");
    }
}

ListSymSet::ListSymSet(ListType _type, Symbol _open, Symbol _close, Symbol _sep)
        : type(_type), open(_open), close(_close), sep(_sep)
{
}

const std::map<Symbol, ListSymSet> list_sym_sets{ []() {
    const std::vector<ListSymSet> sets{
        { ListType::PAREN,
          Symbol::PAREN_OPEN,
          Symbol::PAREN_CLOSE,
          Symbol::COMMA },
        { ListType::SQUARE,
          Symbol::SQUARE_OPEN,
          Symbol::SQUARE_CLOSE,
          Symbol::COMMA },
        { ListType::CURLY,
          Symbol::CURLY_OPEN,
          Symbol::CURLY_CLOSE,
          Symbol::COMMA },
    };
    std::map<Symbol, ListSymSet> m;
    for (const auto& set : sets) {
        m.emplace(set.open, set);
    }
    return m;
}() };

static const std::map<Symbol, poly::string_view> syms_to_debug_strs{
    { Symbol::NL, "NL" },
    { Symbol::COMMA, "COMMA" },
    { Symbol::PAREN_OPEN, "PAREN_OPEN" },
    { Symbol::PAREN_CLOSE, "PAREN_CLOSE" },
    { Symbol::CURLY_OPEN, "CURLY_OPEN" },
    { Symbol::CURLY_CLOSE, "CURLY_CLOSE" },
    { Symbol::SQUARE_OPEN, "SQUARE_OPEN" },
    { Symbol::SQUARE_CLOSE, "SQUARE_CLOSE" },

    { Symbol::EXPECT, "EXPECT" },
    { Symbol::SIZEOF, "SIZEOF" },
    { Symbol::ASSIGN, "ASSIGN" },
    { Symbol::ASSUME, "ASSUME" },
    { Symbol::MEMBER, "MEMBER" },

    { Symbol::EQ, "EQ" },
    { Symbol::NE, "NE" },
    { Symbol::GT, "GT" },
    { Symbol::GE, "GE" },
    { Symbol::LT, "LT" },
    { Symbol::LE, "LE" },
    { Symbol::ADD, "ADD" },
    { Symbol::SUB, "SUB" },
    { Symbol::MUL, "MUL" },
    { Symbol::DIV, "DIV" },
    { Symbol::MOD, "MOD" },

    { Symbol::BIT_AND, "BIT_AND" },
    { Symbol::BIT_OR, "BIT_OR" },
    { Symbol::BIT_XOR, "BIT_XOR" },
    { Symbol::BIT_NOT, "BIT_NOT" },

    { Symbol::LOG_AND, "LOG_AND" },
    { Symbol::LOG_OR, "LOG_OR" },
    { Symbol::LOG_NOT, "LOG_NOT" },

    { Symbol::BYTE, "BYTE" },
    { Symbol::U8, "U8" },
    { Symbol::I8, "I8" },
    { Symbol::U16LE, "U16LE" },
    { Symbol::U16BE, "U16BE" },
    { Symbol::I16LE, "I16LE" },
    { Symbol::I16BE, "I16BE" },
    { Symbol::U32LE, "U32LE" },
    { Symbol::U32BE, "U32BE" },
    { Symbol::I32LE, "I32LE" },
    { Symbol::I32BE, "I32BE" },
    { Symbol::U64LE, "U64LE" },
    { Symbol::U64BE, "U64BE" },
    { Symbol::I64LE, "I64LE" },
    { Symbol::I64BE, "I64BE" },

    { Symbol::F16LE, "F16LE" },
    { Symbol::F16BE, "F16BE" },
    { Symbol::F32LE, "F32LE" },
    { Symbol::F32BE, "F32BE" },
    { Symbol::F64LE, "F64LE" },
    { Symbol::F64BE, "F64BE" },

    { Symbol::BF16LE, "BF16LE" },
    { Symbol::BF16BE, "BF16BE" },

    { Symbol::BYTES, "BYTES" },
    { Symbol::RECORD, "RECORD" },

    { Symbol::WHEN, "WHEN" },
};

poly::string_view sym_to_debug_str(Symbol sym)
{
    try {
        return syms_to_debug_strs.at(sym);
    } catch (const std::out_of_range&) {
        static const poly::string_view unknown{ "UNKNOWN???" };
        return unknown;
    }
}

/* non-static: this is exposed */
const std::vector<std::pair<poly::string_view, Symbol>> strs_to_syms{
    { ",", Symbol::COMMA },
    { "(", Symbol::PAREN_OPEN },
    { ")", Symbol::PAREN_CLOSE },
    { "{", Symbol::CURLY_OPEN },
    { "}", Symbol::CURLY_CLOSE },
    { "[", Symbol::SQUARE_OPEN },
    { "]", Symbol::SQUARE_CLOSE },
    { "==", Symbol::EQ },
    { "!=", Symbol::NE },
    { ">=", Symbol::GE },
    { ">", Symbol::GT },
    { "<=", Symbol::LE },
    { "<", Symbol::LT },
    { "=", Symbol::ASSIGN },
    { "+", Symbol::ADD },
    { "-", Symbol::SUB },
    { "*", Symbol::MUL },
    { "/", Symbol::DIV },
    { "%", Symbol::MOD },
    { "&&", Symbol::LOG_AND },
    { "||", Symbol::LOG_OR },
    { "!", Symbol::LOG_NOT },
    { "&", Symbol::BIT_AND },
    { "|", Symbol::BIT_OR },
    { "^", Symbol::BIT_XOR },
    { "~", Symbol::BIT_NOT },
    { ":", Symbol::ASSUME },
    { ".", Symbol::MEMBER },
    { "expect", Symbol::EXPECT },
    { "sizeof", Symbol::SIZEOF },

    { "Byte", Symbol::BYTE },
    { "UInt8", Symbol::U8 },
    { "Int8", Symbol::I8 },
    { "UInt16LE", Symbol::U16LE },
    { "UInt16BE", Symbol::U16BE },
    { "Int16LE", Symbol::I16LE },
    { "Int16BE", Symbol::I16BE },
    { "UInt32LE", Symbol::U32LE },
    { "UInt32BE", Symbol::U32BE },
    { "Int32LE", Symbol::I32LE },
    { "Int32BE", Symbol::I32BE },
    { "UInt64LE", Symbol::U64LE },
    { "UInt64BE", Symbol::U64BE },
    { "Int64LE", Symbol::I64LE },
    { "Int64BE", Symbol::I64BE },
    { "Float16LE", Symbol::F16LE },
    { "Float16BE", Symbol::F16BE },
    { "Float32LE", Symbol::F32LE },
    { "Float32BE", Symbol::F32BE },
    { "Float64LE", Symbol::F64LE },
    { "Float64BE", Symbol::F64BE },
    { "BFloat16LE", Symbol::BF16LE },
    { "BFloat16BE", Symbol::BF16BE },
    { "Bytes", Symbol::BYTES },
    { "Record", Symbol::RECORD },
    { "when", Symbol::WHEN },
};

/* These symbols can't actually be accessed via these names. */
static const std::vector<std::pair<poly::string_view, Symbol>>
        addl_strs_to_syms{
            { "\\n", Symbol::NL },

        };

static const std::map<Symbol, poly::string_view> syms_to_repr_strs{ []() {
    std::map<Symbol, poly::string_view> m;
    for (const auto& pair : strs_to_syms) {
        m.emplace(pair.second, pair.first);
    }
    for (const auto& pair : addl_strs_to_syms) {
        m.emplace(pair.second, pair.first);
    }
    return m;
}() };

poly::string_view sym_to_repr_str(Symbol sym)
{
    try {
        return syms_to_repr_strs.at(sym);
    } catch (const std::out_of_range&) {
        throw InvariantViolation(
                "Lookup failed in sym_to_repr_str(Symbol::"
                + std::string{ sym_to_debug_str(sym) } + ")");
    }
}
} // namespace openzl::sddl2
