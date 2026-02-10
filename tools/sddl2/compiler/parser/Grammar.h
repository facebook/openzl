// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <vector>

#include "openzl/cpp/poly/Optional.hpp"
#include "openzl/cpp/poly/StringView.hpp"

#include "tools/sddl2/compiler/Syntax.h"
#include "tools/sddl2/compiler/parser/AST.h"

namespace openzl::sddl2 {

/**
 * SDDL operators match C/C++ precedence and associativity, so expressions
 * should be parsed basically the same.
 */

enum class Precedence : size_t {
    NULLARY,

    ACCESS,

    UNARY,

    MUL_DIV_MOD,
    ADD_SUB,
    RELATION,
    EQUALITY,

    BIT_AND,
    BIT_XOR,
    BIT_OR,

    LOG_AND,
    LOG_OR,

    ASSIGNMENT,
};

enum class Associativity {
    LEFT_TO_RIGHT,
    RIGHT_TO_LEFT,
};

enum class ArgType {
    SYM,
    LIST_PAREN,
    LIST_SQUARE,
    LIST_CURLY,
    EXPR,
};

poly::string_view precedence_to_str(Precedence precedence);

poly::string_view associativity_to_str(Associativity associativity);

poly::string_view arg_type_to_str(ArgType arg_type);

class GrammarRule {
   public:
    virtual ~GrammarRule() = default;

    Symbol sym() const;
    Precedence precedence() const;
    Associativity associativity() const;

    /// The types of arguments that this rule accepts.
    std::vector<ArgType> arg_types() const&;
    size_t num_args() const;
    size_t num_rhs_args() const;
    bool has_lhs_arg() const;

    poly::string_view sym_str() const;
    poly::string_view precedence_str() const;
    poly::string_view associativity_str() const;

    std::string arg_types_str() const;

    /// An assemblage of the above strings into one record.
    std::string info_str() const;

    /**
     * Apply this rule and construct an ASTNode from the op and args.
     */
    ASTPtr gen(ASTPtr op, std::vector<ASTPtr> args) const;

    poly::optional<ASTPtr> match_arg(const ASTPtr& op, ASTPtr arg, size_t idx)
            const;

   protected:
    GrammarRule(
            Symbol sym,
            Precedence precedence,
            std::vector<ArgType> arg_types,
            bool has_lhs_arg = false);

   private:
    virtual ASTPtr do_gen(ASTPtr op, std::vector<ASTPtr> args) const = 0;

    virtual poly::optional<ASTPtr>
    do_match_arg(const ASTPtr& op, ASTPtr arg, size_t idx) const;

   private:
    /// The symbol that this rule matches.
    const Symbol sym_;

    const Precedence precedence_;
    const Associativity associativity_;
    const std::vector<ArgType> arg_types_;

    /// True if the first argument is the left-hand side of the operator.
    bool has_lhs_arg_ = false;
};

const std::vector<std::reference_wrapper<const GrammarRule>>& sym_to_rules(
        Symbol symbol);

const std::vector<std::reference_wrapper<const GrammarRule>>&
list_type_to_rules(ListType list_type);

} // namespace openzl::sddl2
