// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl/compiler/Token.h"

#include <iomanip>
#include <sstream>

#include "tools/sddl/compiler/Utils.h"

using namespace openzl::sddl::detail;

namespace openzl::sddl {

Token::Token(SourceLocation loc, Symbol sym)
        : loc_(std::move(loc)), type_(Type::SYM), value_({ .sym = sym })
{
}

Token::Token(SourceLocation loc, poly::string_view sv)
        : loc_(std::move(loc)), type_(Type::WORD), value_({ .word = sv })
{
}

Token::Token(SourceLocation loc, int64_t num)
        : loc_(std::move(loc)), type_(Type::NUM), value_({ .num = num })
{
}

bool Token::operator==(const Token& o) const
{
    if (type_ != o.type_) {
        return false;
    }
    switch (type_) {
        case Type::SYM:
            return value_.sym == o.value_.sym;
        case Type::WORD:
            return value_.word == o.value_.word;
        case Type::NUM:
            return value_.num == o.value_.num;
        default:
            throw InvariantViolation(loc_, "Invalid Token type!");
    }
}

bool Token::operator==(const Symbol& o) const
{
    return is_sym() && sym() == o;
}

bool Token::is_sym() const
{
    return type_ == Type::SYM;
}

Symbol Token::sym() const
{
    return value_.sym;
}

bool Token::is_word() const
{
    return type_ == Type::WORD;
}

const poly::string_view& Token::word() const
{
    return value_.word;
}

bool Token::is_num() const
{
    return type_ == Type::NUM;
}

const int64_t& Token::num() const
{
    return value_.num;
}

std::string Token::str(size_t indent) const
{
    std::stringstream ss;
    ss << std::string(indent, ' ');
    visit([&ss](const auto& val) {
        using T = std::decay_t<decltype(val)>;
        if constexpr (std::is_same_v<T, Symbol>) {
            ss << "Symbol: ";
            ss << sym_to_debug_str(val);
        } else if constexpr (std::is_same_v<T, poly::string_view>) {
            ss << "Word: ";
            ss << std::quoted(val);
        } else if constexpr (std::is_same_v<T, int64_t>) {
            ss << "Num: ";
            ss << std::to_string(val);
        } else {
#if __cplusplus >= 201703L
            static_assert(dependent_false_v<T>, "Non-exhaustive visitor!");
#else
            throw InvariantViolation(loc_, "Non-exhaustive visitor!");
#endif
        }
    });
    ss << "\n"
       << std::string(indent + 2, ' ') << "at " << loc_.pos_str() << ":\n"
       << loc_.contents_str(indent + 2);
    return std::move(ss).str();
}

const SourceLocation& Token::loc() const
{
    return loc_;
}

} // namespace openzl::sddl
