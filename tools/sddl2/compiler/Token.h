// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstdint>
#include <string>
#include <type_traits>

#include "tools/sddl/compiler/Exception.h"
#include "tools/sddl/compiler/Source.h"
#include "tools/sddl/compiler/Syntax.h"

namespace openzl::sddl {

class Token {
   public:
    Token(SourceLocation loc, Symbol sym);

    Token(SourceLocation loc, poly::string_view sv);

    Token(SourceLocation loc, int64_t num);

    bool operator==(const Token& o) const;

    bool operator==(const Symbol& sym) const;

    bool is_sym() const;

    Symbol sym() const;

    bool is_word() const;

    const poly::string_view& word() const;

    bool is_num() const;

    const int64_t& num() const;

    template <typename Func>
    auto visit(Func&& func) const
    {
        switch (type_) {
            case Type::SYM:
                return func(value_.sym);
            case Type::WORD:
                return func(value_.word);
            case Type::NUM:
                return func(value_.num);
            default:
                throw InvariantViolation(loc_, "Invalid Token type!");
        }
    }

    std::string str(size_t indent = 0) const;

    const SourceLocation& loc() const;

   private:
    enum class Type {
        SYM,
        WORD,
        NUM,
    };

    const SourceLocation loc_;
    const Type type_;
    const union {
        Symbol sym;
        poly::string_view word;
        int64_t num;
    } value_;
};

} // namespace openzl::sddl
