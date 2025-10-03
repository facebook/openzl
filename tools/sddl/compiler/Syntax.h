// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <map>
#include <vector>

#include "openzl/cpp/poly/Optional.hpp"
#include "openzl/cpp/poly/StringView.hpp"

namespace openzl::sddl {

enum class Symbol {
    // Grouping Tokens
    NL,           // \n
    SEMI,         // ;
    COMMA,        // ,
    PAREN_OPEN,   // (
    PAREN_CLOSE,  // )
    CURLY_OPEN,   // {
    CURLY_CLOSE,  // }
    SQUARE_OPEN,  // [
    SQUARE_CLOSE, // ]

    // Operators
    DIE,
    EXPECT,
    CONSUME,
    SIZEOF,
    SEND,
    ASSIGN,

    ASSUME, // fused assign and consume

    MEMBER,

    BIND,

    NEG, // `-` is tokenized as SUB, but the unary form is converted into this
         // during parsing.

    EQ,
    NE,
    ADD,
    SUB,
    MUL,
    DIV,
    MOD,

    // Keywords

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

    // Other Fields
    POISON,
    ATOM,
    RECORD,
    ARRAY,

    DEST,
};

enum class SymbolType {
    GROUPING,
    OPERATOR,
    KEYWORD,
};

SymbolType sym_type(Symbol symbol);

/// @returns a name string for a symbol.
/// (E.g., Symbol::ADD -> "ADD")
poly::string_view sym_to_debug_str(Symbol symbol);

/// @returns the representation of a symbol that would appear in source code.
/// (E.g., Symbol::ADD -> "+")
poly::string_view sym_to_repr_str(Symbol symbol);

/// @returns the string used to represent a symbol in the serialized CBOR.
/// (E.g., Symbol::U64LE -> "u8l")
poly::string_view sym_to_ser_str(Symbol symbol);

/**
 * This is a vector not a map because some operators are prefixes of others, so
 * we have to check the longer ones first.
 */
extern const std::vector<std::pair<poly::string_view, Symbol>> strs_to_syms;

enum class ListType {
    PAREN,
    SQUARE,
    CURLY,
};

poly::string_view list_type_to_debug_str(ListType list_type);

/**
 * Describes the opening, closing, and separator symbols that define a list.
 * E.g., '(', ')', and ',' for your standard parenthesized list.
 */
struct ListSymSet {
    ListSymSet(ListType type, Symbol open, Symbol close, Symbol sep);

    ListType type;
    Symbol open;
    Symbol close;
    Symbol sep;
};

/// Map to look up a ListSymSet from its opening symbol.
extern const std::map<Symbol, ListSymSet> list_sym_sets;

} // namespace openzl::sddl
