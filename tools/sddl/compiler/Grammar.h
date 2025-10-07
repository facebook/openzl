// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <map>
#include <vector>

#include "openzl/cpp/poly/StringView.hpp"

#include "tools/sddl/compiler/AST.h"
#include "tools/sddl/compiler/Syntax.h"

namespace openzl::sddl {

/**
 * SDDL operators match C/C++ precedence and associativity, so expressions
 * should be parsed basically the same.
 */

enum class Precedence : size_t {
    CONTROL_FLOW,

    NULLARY,

    ACCESS,

    UNARY,

    MUL_DIV_MOD,
    ADD_SUB,
    RELATION,
    EQUALITY,

    ASSIGNMENT,
};

enum class Associativity {
    LEFT_TO_RIGHT,
    RIGHT_TO_LEFT,
};

enum class Arity {
    NULLARY,
    PREFIX_UNARY,
    INFIX_BINARY,
};

enum class ArgType {
    NONE,
    LIST_PAREN,
    LIST_SQUARE,
    LIST_CURLY,
    EXPR,
};

poly::string_view precedence_to_str(Precedence precedence);

poly::string_view associativity_to_str(Associativity associativity);

poly::string_view arity_to_str(Arity arity);

poly::string_view arg_type_to_str(ArgType arg_type);

class GrammarRule {
   public:
    virtual ~GrammarRule() = default;

    Symbol op() const;
    Precedence precedence() const;
    Associativity associativity() const;
    Arity arity() const;
    ArgType lhs_type() const;
    ArgType rhs_type() const;

    poly::string_view op_str() const;
    poly::string_view precedence_str() const;
    poly::string_view associativity_str() const;
    poly::string_view arity_str() const;
    poly::string_view lhs_type_str() const;
    poly::string_view rhs_type_str() const;

    /// An assemblage of the above strings into one record.
    std::string info_str() const;

    /**
     * Apply this rule and construct an ASTNode from the op and args.
     */
    ASTPtr gen(ASTPtr op, ASTPtr lhs, ASTPtr rhs) const;

    poly::optional<ASTPtr> match_lhs(const ASTPtr& op, ASTPtr arg) const;

    poly::optional<ASTPtr> match_rhs(const ASTPtr& op, ASTPtr arg) const;

   protected:
    GrammarRule(
            Symbol op,
            Precedence precedence,
            ArgType lhs_type,
            ArgType rhs_type);

   private:
    virtual ASTPtr do_gen(ASTPtr op, ASTPtr lhs, ASTPtr rhs) const = 0;

    /// Can set custom matching logic on top of the matching done in @ref
    /// lhs_matches, defaults to permissive.
    virtual poly::optional<ASTPtr> do_match_lhs(const ASTPtr& op, ASTPtr arg)
            const;

    /// Can set custom matching logic on top of the matching done in @ref
    /// rhs_matches, defaults to permissive.
    virtual poly::optional<ASTPtr> do_match_rhs(const ASTPtr& op, ASTPtr arg)
            const;

   private:
    const Symbol op_;
    const Precedence precedence_;
    const Associativity associativity_;
    const Arity arity_;
    const ArgType lhs_type_;
    const ArgType rhs_type_;
};

const std::vector<std::reference_wrapper<const GrammarRule>>& sym_to_rules(
        Symbol symbol);

const std::vector<std::reference_wrapper<const GrammarRule>>&
list_type_to_implicit_rules(ListType list_type);

bool sym_is_always_binary_op(Symbol symbol);

} // namespace openzl::sddl
