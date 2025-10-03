// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl/compiler/Grammar.h"

#include <sstream>
#include <string>

#include "openzl/shared/portability.h"

#include "tools/sddl/compiler/Exception.h"
#include "tools/sddl/compiler/Utils.h"

using namespace openzl::sddl::detail;

namespace openzl::sddl {

namespace {

Arity arity_of(ArgType lhs_type, ArgType rhs_type)
{
    if (lhs_type == ArgType::NONE) {
        if (rhs_type == ArgType::NONE) {
            return Arity::NULLARY;
        } else {
            return Arity::PREFIX_UNARY;
        }
    } else {
        if (rhs_type == ArgType::NONE) {
            throw InvariantViolation(
                    "Postfix unary operators aren't supported!");
        } else {
            return Arity::INFIX_BINARY;
        }
    }
}

Token token_of(const ASTPtr& node)
{
    const auto sym = some(node).as_sym();
    if (sym == nullptr) {
        throw InvariantViolation(some(node).loc(), "Expected token.");
    }
    return Token{ sym->loc(), **sym };
}

constexpr Associativity LTR = Associativity::LEFT_TO_RIGHT;
constexpr Associativity RTL = Associativity::RIGHT_TO_LEFT;

const std::map<Precedence, Associativity> associativities{
    { Precedence::ACCESS, LTR },

    { Precedence::UNARY, RTL },       { Precedence::NULLARY, LTR },

    { Precedence::MUL_DIV_MOD, LTR }, { Precedence::ADD_SUB, LTR },
    { Precedence::RELATION, LTR },    { Precedence::EQUALITY, LTR },

    { Precedence::ASSIGNMENT, RTL },
};

const std::map<ArgType, ListType> arg_types_to_list_types{
    { ArgType::LIST_PAREN, ListType::PAREN },
    { ArgType::LIST_SQUARE, ListType::SQUARE },
    { ArgType::LIST_CURLY, ListType::CURLY },
};

template <typename T>
std::string enum_to_str_of_int(T e)
{
    return std::to_string(static_cast<int>(e));
}

const std::map<Precedence, std::string> precedences_to_strs{
    { Precedence::UNARY,
      "UNARY(" + enum_to_str_of_int(Precedence::UNARY) + ")" },
    { Precedence::NULLARY,
      "NULLARY(" + enum_to_str_of_int(Precedence::NULLARY) + ")" },
    { Precedence::ACCESS,
      "ACCESS(" + enum_to_str_of_int(Precedence::ACCESS) + ")" },
    { Precedence::MUL_DIV_MOD,
      "MUL_DIV_MOD(" + enum_to_str_of_int(Precedence::MUL_DIV_MOD) + ")" },
    { Precedence::ADD_SUB,
      "ADD_SUB(" + enum_to_str_of_int(Precedence::ADD_SUB) + ")" },
    { Precedence::RELATION,
      "RELATION(" + enum_to_str_of_int(Precedence::RELATION) + ")" },
    { Precedence::EQUALITY,
      "EQUALITY(" + enum_to_str_of_int(Precedence::EQUALITY) + ")" },
    { Precedence::ASSIGNMENT,
      "ASSIGNMENT(" + enum_to_str_of_int(Precedence::ASSIGNMENT) + ")" },
};

const std::map<Associativity, poly::string_view> associativities_to_strs{
    { Associativity::LEFT_TO_RIGHT, "LEFT_TO_RIGHT" },
    { Associativity::RIGHT_TO_LEFT, "RIGHT_TO_LEFT" },
};

const std::map<Arity, poly::string_view> arities_to_strs{
    { Arity::NULLARY, "NULLARY" },
    { Arity::PREFIX_UNARY, "PREFIX_UNARY" },
    { Arity::INFIX_BINARY, "INFIX_BINARY" },
};

const std::map<ArgType, poly::string_view> arg_types_to_strs{
    { ArgType::NONE, "NONE" },
    { ArgType::LIST_PAREN, "LIST_PAREN" },
    { ArgType::LIST_SQUARE, "LIST_SQUARE" },
    { ArgType::LIST_CURLY, "LIST_CURLY" },
    { ArgType::EXPR, "EXPR" },
};

/**
 * Helper to build a synthetic AST tree rather than translating tokens 1:1.
 */
class Codegen {
   public:
    explicit Codegen(SourceLocation loc) : loc_(std::move(loc)) {}

    Token token(Symbol sym) const
    {
        return Token{ loc_, sym };
    }

    template <typename... Args>
    ASTVec vec(Args... args) const
    {
        return ASTVec{ std::move(args)... };
    }

    template <typename... Args>
    ASTPtr op(Symbol sym, Args... args) const
    {
        return std::make_shared<ASTOp>(token(sym), vec(std::move(args)...));
    }

    // Ops

    ASTPtr expect(ASTPtr arg) const
    {
        return op(Symbol::EXPECT, std::move(arg));
    }

    ASTPtr consume(ASTPtr arg) const
    {
        return op(Symbol::CONSUME, std::move(arg));
    }

    ASTPtr size_of(ASTPtr arg) const
    {
        return op(Symbol::SIZEOF, std::move(arg));
    }

    ASTPtr send(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Symbol::SEND, std::move(lhs), std::move(rhs));
    }

    ASTPtr assign(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Symbol::ASSIGN, std::move(lhs), std::move(rhs));
    }

    ASTPtr member(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Symbol::MEMBER, std::move(lhs), std::move(rhs));
    }

    ASTPtr bind(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Symbol::BIND, std::move(lhs), std::move(rhs));
    }

    ASTPtr neg(ASTPtr arg) const
    {
        return op(Symbol::NEG, std::move(arg));
    }

    ASTPtr eq(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Symbol::EQ, std::move(lhs), std::move(rhs));
    }

    ASTPtr ne(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Symbol::NE, std::move(lhs), std::move(rhs));
    }

    ASTPtr add(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Symbol::ADD, std::move(lhs), std::move(rhs));
    }

    ASTPtr sub(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Symbol::SUB, std::move(lhs), std::move(rhs));
    }

    ASTPtr mul(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Symbol::MUL, std::move(lhs), std::move(rhs));
    }

    ASTPtr div(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Symbol::DIV, std::move(lhs), std::move(rhs));
    }

    ASTPtr mod(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Symbol::MOD, std::move(lhs), std::move(rhs));
    }

    // Other types of things

    ASTPtr num(int64_t val) const
    {
        return std::make_shared<ASTNum>(Token{ loc_, val });
    }

    ASTPtr array(ASTPtr field, ASTPtr len) const
    {
        return std::make_shared<ASTArray>(std::move(field), std::move(len));
    }

    ASTPtr record(ASTVec fields) const
    {
        return std::make_shared<ASTRecord>(curly_list(std::move(fields)));
    }

    ASTPtr dest() const
    {
        return std::make_shared<ASTDest>(Token{ loc_, 0 }, nullptr);
    }

    ASTPtr var(poly::string_view name) const
    {
        return std::make_shared<ASTVar>(Token{ loc_, name });
    }

    ASTPtr tuple(poly::string_view name) const
    {
        return std::make_shared<ASTVar>(Token{ loc_, name });
    }

    ASTPtr list(Symbol open_sym, ASTVec elts) const
    {
        const auto& list_sym_set = list_sym_sets.at(open_sym);
        return std::make_shared<ASTList>(
                list_sym_set.type,
                std::make_shared<ASTSym>(token(list_sym_set.open)),
                std::make_shared<ASTSym>(token(list_sym_set.close)),
                std::move(elts));
    }

    ASTPtr paren_list(ASTVec elts) const
    {
        return list(Symbol::PAREN_OPEN, std::move(elts));
    }

    ASTPtr square_list(ASTVec elts) const
    {
        return list(Symbol::SQUARE_OPEN, std::move(elts));
    }

    ASTPtr curly_list(ASTVec elts) const
    {
        return list(Symbol::CURLY_OPEN, std::move(elts));
    }

    ASTPtr tuple(ASTVec elts) const
    {
        return std::make_shared<ASTTuple>(paren_list(std::move(elts)));
    }

    ASTPtr func(ASTVec args, ASTVec body) const
    {
        const auto args_ast   = paren_list(std::move(args));
        const auto body_ast   = curly_list(std::move(body));
        const auto& args_list = some(args_ast).as_list();
        const auto& body_list = some(body_ast).as_list();
        return std::make_shared<ASTFunc>(
                args_list->nodes(), body_list->nodes());
    }

   private:
    const SourceLocation loc_;
};

} // anonymous namespace

poly::string_view precedence_to_str(Precedence precedence)
{
    return precedences_to_strs.at(precedence);
}

poly::string_view associativity_to_str(Associativity associativity)
{
    return associativities_to_strs.at(associativity);
}

poly::string_view arity_to_str(Arity arity)
{
    return arities_to_strs.at(arity);
}

poly::string_view arg_type_to_str(ArgType arg_type)
{
    return arg_types_to_strs.at(arg_type);
}

GrammarRule::GrammarRule(
        Symbol op,
        Precedence precedence,
        ArgType lhs_type,
        ArgType rhs_type)
        : op_(op),
          precedence_(precedence),
          associativity_(associativities.at(precedence)),
          arity_(arity_of(lhs_type, rhs_type)),
          lhs_type_(lhs_type),
          rhs_type_(rhs_type)
{
}

Symbol GrammarRule::op() const
{
    return op_;
}

Precedence GrammarRule::precedence() const
{
    return precedence_;
}

Associativity GrammarRule::associativity() const
{
    return associativity_;
}

Arity GrammarRule::arity() const
{
    return arity_;
}

ArgType GrammarRule::lhs_type() const
{
    return lhs_type_;
}

ArgType GrammarRule::rhs_type() const
{
    return rhs_type_;
}

poly::string_view GrammarRule::op_str() const
{
    return sym_to_debug_str(op());
}

poly::string_view GrammarRule::precedence_str() const
{
    return precedence_to_str(precedence());
}

poly::string_view GrammarRule::associativity_str() const
{
    return associativity_to_str(associativity());
}

poly::string_view GrammarRule::arity_str() const
{
    return arity_to_str(arity());
}

poly::string_view GrammarRule::lhs_type_str() const
{
    return arg_type_to_str(lhs_type());
}

poly::string_view GrammarRule::rhs_type_str() const
{
    return arg_type_to_str(rhs_type());
}

std::string GrammarRule::info_str() const
{
    std::stringstream ss;
    ss << "GrammarRule(";
    ss << "Symbol::" << op_str() << ", ";
    ss << "Precedence::" << precedence_str() << ", ";
    ss << "Associativity::" << associativity_str() << ", ";
    ss << "Arity::" << arity_str() << ", ";
    ss << "ArgType::" << lhs_type_str() << ", ";
    ss << "ArgType::" << rhs_type_str();
    ss << ")";
    return std::move(ss).str();
}

ASTPtr GrammarRule::gen(ASTPtr op, ASTPtr lhs, ASTPtr rhs) const
{
    const auto lt = lhs_type();
    const auto rt = rhs_type();
    if ((lt == ArgType::NONE) != (lhs == nullptr)) {
        if (lhs) {
            throw InvariantViolation(
                    some(op).loc(),
                    "Unexpectedly received left-hand argument when this rule doesn't expect one.");
        } else {
            throw InvariantViolation(
                    some(op).loc(),
                    "Got null left-hand argument when this rule expects one.");
        }
    }
    if ((rt == ArgType::NONE) != (rhs == nullptr)) {
        if (rhs) {
            throw InvariantViolation(
                    some(op).loc(),
                    "Unexpectedly received right-hand argument when this rule doesn't expect one.");
        } else {
            throw InvariantViolation(
                    some(op).loc(),
                    "Got null right-hand argument when this rule expects one.");
        }
    }

    {
        auto maybe_lhs = match_lhs(op, std::move(lhs));
        if (!maybe_lhs) {
            throw InvariantViolation(
                    some(op).loc(),
                    "Left-hand argument failed to match while the op was being generated, i.e., after it should already have successfully been matched!");
        }
        lhs = std::move(maybe_lhs).value();
    }
    {
        auto maybe_rhs = match_rhs(op, std::move(rhs));
        if (!maybe_rhs) {
            throw InvariantViolation(
                    some(op).loc(),
                    "Right-hand argument failed to match while the op was being generated, i.e., after it should already have successfully been matched!");
        }
        rhs = std::move(maybe_rhs).value();
    }

    auto result = do_gen(std::move(op), std::move(lhs), std::move(rhs));
    some(result);
    return result;
}

poly::optional<ASTPtr> GrammarRule::match_lhs(const ASTPtr& op, ASTPtr arg)
        const
{
    const auto type = lhs_type();
    switch (type) {
        case ArgType::NONE:
            if (arg != nullptr) {
                return poly::nullopt;
            }
            break;
        case ArgType::LIST_SQUARE:
        case ArgType::LIST_CURLY:
            arg = unwrap_parens(std::move(arg));
            ZL_FALLTHROUGH;
        case ArgType::LIST_PAREN: {
            const auto* const list = some(arg).as_list();
            if (list == nullptr) {
                return poly::nullopt;
            }
            const auto list_type = arg_types_to_list_types.at(type);
            if (list->list_type() != list_type) {
                return poly::nullopt;
            }
            break;
        }
        case ArgType::EXPR: {
            if (some(arg).as_sym() != nullptr) {
                return poly::nullopt;
            }
            arg = unwrap_parens(std::move(arg));
            if (some(arg).as_list() != nullptr) {
                return poly::nullopt;
            }
            break;
        }
        default:
            throw InvariantViolation(
                    some(op).loc() + some(arg).loc(), "Illegal ArgType!");
    }

    return do_match_lhs(op, std::move(arg));
}

poly::optional<ASTPtr> GrammarRule::match_rhs(const ASTPtr& op, ASTPtr arg)
        const
{
    const auto type = rhs_type();
    switch (type) {
        case ArgType::NONE:
            if (arg != nullptr) {
                return poly::nullopt;
            }
            break;
        case ArgType::LIST_SQUARE:
        case ArgType::LIST_CURLY:
            arg = unwrap_parens(std::move(arg));
            ZL_FALLTHROUGH;
        case ArgType::LIST_PAREN: {
            const auto* const list = some(arg).as_list();
            if (list == nullptr) {
                return poly::nullopt;
            }
            const auto list_type = arg_types_to_list_types.at(type);
            if (list->list_type() != list_type) {
                return poly::nullopt;
            }
            break;
        }
        case ArgType::EXPR: {
            if (some(arg).as_sym() != nullptr) {
                return poly::nullopt;
            }
            arg = unwrap_parens(arg);
            if (some(arg).as_list() != nullptr) {
                return poly::nullopt;
            }
            break;
        }
        default:
            throw InvariantViolation(
                    some(op).loc() + some(arg).loc(), "Illegal ArgType!");
    }

    return do_match_rhs(op, std::move(arg));
}

poly::optional<ASTPtr> GrammarRule::do_match_lhs(const ASTPtr&, ASTPtr arg)
        const
{
    return std::move(arg);
}

poly::optional<ASTPtr> GrammarRule::do_match_rhs(const ASTPtr&, ASTPtr arg)
        const
{
    return std::move(arg);
}

namespace {

class BuiltInFieldRule : public GrammarRule {
   public:
    explicit BuiltInFieldRule(Symbol op)
            : GrammarRule(op, Precedence::NULLARY, ArgType::NONE, ArgType::NONE)
    {
    }

   private:
    ASTPtr do_gen(ASTPtr op, ASTPtr, ASTPtr) const override
    {
        ASTVec args{
            std::make_shared<ASTBuiltinField>(token_of(op)),
            std::make_shared<ASTDest>(token_of(op), nullptr),
        };

        return std::make_shared<ASTOp>(
                Token{ op->loc(), Symbol::SEND }, std::move(args));
    }
};

class PoisonRule : public GrammarRule {
   public:
    explicit PoisonRule()
            : GrammarRule(
                      Symbol::POISON,
                      Precedence::NULLARY,
                      ArgType::NONE,
                      ArgType::NONE)
    {
    }

   private:
    ASTPtr do_gen(ASTPtr op, ASTPtr, ASTPtr) const override
    {
        return std::make_shared<ASTPoison>(token_of(op));
    }
};

class RecordRule : public GrammarRule {
   public:
    explicit RecordRule()
            : GrammarRule(
                      Symbol::RECORD,
                      Precedence::UNARY,
                      ArgType::NONE,
                      ArgType::LIST_CURLY)
    {
    }

   private:
    ASTPtr do_gen(ASTPtr, ASTPtr, ASTPtr rhs) const override
    {
        return std::make_shared<ASTRecord>(std::move(rhs));
    }
};

class FuncRule : public GrammarRule {
   public:
    explicit FuncRule()
            : GrammarRule(
                      Symbol::RECORD,
                      Precedence::ACCESS,
                      ArgType::LIST_PAREN,
                      ArgType::LIST_CURLY)
    {
    }

   private:
    ASTPtr do_gen(ASTPtr, ASTPtr lhs, ASTPtr rhs) const override
    {
        const auto& args = some(lhs).as_list();
        const auto& body = some(rhs).as_list();
        return std::make_shared<ASTFunc>(args->nodes(), body->nodes());
    }
};

class ArrayRule : public GrammarRule {
   public:
    explicit ArrayRule()
            : GrammarRule(
                      Symbol::ARRAY,
                      Precedence::ACCESS,
                      ArgType::EXPR,
                      ArgType::LIST_SQUARE)
    {
    }

   private:
    ASTPtr do_gen(ASTPtr, ASTPtr lhs, ASTPtr rhs) const override
    {
        const auto& rhs_nodes                      = unwrap_square(rhs);
        constexpr bool allow_implicit_array_sizing = true;
        if (allow_implicit_array_sizing && rhs_nodes.size() == 0) {
            const Codegen cg{ maybe_loc(lhs) + maybe_loc(rhs) };

            /**
             * This expands:
             * ```
             * field[]
             * ```
             * into
             * ```
             * (: (__field, __rem) {
             *   __size = sizeof __field
             *   expect __rem % __size == 0
             *   __len = __rem / __size
             *   __resolved = __field[__len]
             * } (field, _rem)).__resolved
             * ```
             */

            const auto field_var    = cg.var("__field");
            const auto rem_var      = cg.var("__rem");
            const auto size_var     = cg.var("__size");
            const auto len_var      = cg.var("__len");
            const auto resolved_var = cg.var("__resolved");
            return cg.member(
                    cg.consume(cg.bind(
                            cg.func(cg.vec(field_var, rem_var),
                                    cg.vec(cg.assign(
                                                   size_var,
                                                   cg.size_of(field_var)),
                                           cg.assign(
                                                   len_var,
                                                   cg.div(rem_var, size_var)),
                                           cg.expect(cg.eq(
                                                   cg.mod(rem_var, size_var),
                                                   cg.num(0))),
                                           cg.assign(
                                                   resolved_var,
                                                   cg.array(
                                                           field_var,
                                                           len_var)))),
                            cg.tuple(cg.vec(std::move(lhs), cg.var("_rem"))))),
                    resolved_var);
        } else if (rhs_nodes.size() != 1) {
            throw ParseError(
                    some(rhs).loc(),
                    "Array declaration right-hand side list must have single element.");
        }
        return std::make_shared<ASTArray>(std::move(lhs), rhs_nodes[0]);
    }
};

class OpRule : public GrammarRule {
   public:
    template <typename... Args>
    explicit OpRule(Args&&... args) : GrammarRule(std::forward<Args>(args)...)
    {
    }

   protected:
    ASTPtr do_gen(ASTPtr op, ASTPtr lhs, ASTPtr rhs) const override
    {
        std::vector<ASTPtr> args;
        switch (arity()) {
            case Arity::INFIX_BINARY:
                args.push_back(std::move(lhs));
                ZL_FALLTHROUGH;
            case Arity::PREFIX_UNARY:
                args.push_back(std::move(rhs));
                ZL_FALLTHROUGH;
            case Arity::NULLARY:
                break;
        }

        auto token = op->as_sym() ? token_of(op)
                                  : Token{ SourceLocation::null(), this->op() };
        return std::make_shared<ASTOp>(token, std::move(args));
    }
};

class NullaryOpRule : public OpRule {
   public:
    explicit NullaryOpRule(Symbol op)
            : OpRule(op, Precedence::NULLARY, ArgType::NONE, ArgType::NONE)
    {
    }
};

class UnaryOpRule : public OpRule {
   public:
    explicit UnaryOpRule(Symbol op, Precedence precedence = Precedence::UNARY)
            : OpRule(op, precedence, ArgType::NONE, ArgType::EXPR)
    {
    }
};

class BinaryOpRule : public OpRule {
   public:
    explicit BinaryOpRule(Symbol op, Precedence precedence)
            : OpRule(op, precedence, ArgType::EXPR, ArgType::EXPR)
    {
    }
};

class BinaryAssumeRule : public BinaryOpRule {
   public:
    explicit BinaryAssumeRule()
            : BinaryOpRule(Symbol::ASSUME, Precedence::ASSIGNMENT)
    {
    }

    ASTPtr do_gen(ASTPtr op, ASTPtr lhs, ASTPtr rhs) const override
    {
        ASTVec args{ std::move(rhs) };
        rhs = std::make_shared<ASTOp>(
                Token{ some(op).loc(), Symbol::CONSUME }, std::move(args));
        args.clear();
        args.push_back(std::move(lhs));
        args.push_back(std::move(rhs));
        return std::make_shared<ASTOp>(
                Token{ some(op).loc(), Symbol::ASSIGN }, std::move(args));
    }
};

class UnaryAssumeRule : public UnaryOpRule {
   public:
    explicit UnaryAssumeRule()
            : UnaryOpRule(Symbol::ASSUME, Precedence::ASSIGNMENT)
    {
    }

    ASTPtr do_gen(ASTPtr op, ASTPtr, ASTPtr rhs) const override
    {
        ASTVec args{ std::move(rhs) };
        return std::make_shared<ASTOp>(
                Token{ some(op).loc(), Symbol::CONSUME }, std::move(args));
    }
};

class NegationRule : public UnaryOpRule {
   public:
    explicit NegationRule() : UnaryOpRule(Symbol::SUB, Precedence::UNARY) {}

    ASTPtr do_gen(ASTPtr op, ASTPtr, ASTPtr rhs) const override
    {
        const auto* const rhs_num = dynamic_cast<const ASTNum*>(&some(rhs));
        if (rhs_num != NULL) {
            // Optimization: if the rhs is a literal number, fold the negation
            // into the literal rather than emit a negation operation on the
            // positive literal.
            return std::make_shared<ASTNum>(
                    Token{ some(op).loc() + rhs->loc(), -rhs_num->val() });
        }

        ASTVec args{ std::move(rhs) };
        return std::make_shared<ASTOp>(
                Token{ some(op).loc(), Symbol::NEG }, std::move(args));
    }
};

class BindRule : public OpRule {
   public:
    explicit BindRule()
            : OpRule(Symbol::BIND,
                     Precedence::ACCESS,
                     ArgType::EXPR,
                     ArgType::LIST_PAREN)
    {
    }

    ASTPtr do_gen(ASTPtr op, ASTPtr lhs, ASTPtr rhs) const override
    {
        return OpRule::do_gen(
                std::move(op),
                std::move(lhs),
                std::make_shared<ASTTuple>(std::move(rhs)));
    }
};

const std::vector<Symbol> builtin_field_ops{
    Symbol::BYTE,  Symbol::U8,    Symbol::I8,    Symbol::U16LE, Symbol::U16BE,
    Symbol::I16LE, Symbol::I16BE, Symbol::U32LE, Symbol::U32BE, Symbol::I32LE,
    Symbol::I32BE, Symbol::U64LE, Symbol::U64BE, Symbol::I64LE, Symbol::I64BE,
};

template <typename RuleT, typename... Args>
void add_rule(
        std::vector<std::unique_ptr<const GrammarRule>>& rules,
        Args&&... args)
{
    rules.push_back(std::make_unique<RuleT>(std::forward<Args>(args)...));
}

const std::vector<std::unique_ptr<const GrammarRule>> grammar_rules{ []() {
    std::vector<std::unique_ptr<const GrammarRule>> r;

    // Types and Dests

    // Built-ins
    for (const auto op : builtin_field_ops) {
        add_rule<BuiltInFieldRule>(r, op);
    }

    // Compound type ops
    // add_rule<RecordRule>(r);
    add_rule<ArrayRule>(r);

    add_rule<PoisonRule>(r);
    // add_rule<AtomRule>(r);
    // add_rule<RecordRule>(r);
    // add_rule<ArrayRule>(r);

    // add_rule<DestRule>(r);

    // Ops

    add_rule<NullaryOpRule>(r, Symbol::DIE);

    add_rule<UnaryOpRule>(r, Symbol::EXPECT, Precedence::ASSIGNMENT);
    add_rule<UnaryOpRule>(r, Symbol::CONSUME);
    add_rule<UnaryOpRule>(r, Symbol::SIZEOF);

    add_rule<NegationRule>(r);

    add_rule<BinaryOpRule>(r, Symbol::SEND, Precedence::ASSIGNMENT);
    add_rule<BinaryOpRule>(r, Symbol::ASSIGN, Precedence::ASSIGNMENT);
    add_rule<BinaryAssumeRule>(r);
    add_rule<UnaryAssumeRule>(r);
    add_rule<BinaryOpRule>(r, Symbol::MEMBER, Precedence::ACCESS);

    add_rule<BinaryOpRule>(r, Symbol::EQ, Precedence::EQUALITY);
    add_rule<BinaryOpRule>(r, Symbol::NE, Precedence::EQUALITY);

    add_rule<BinaryOpRule>(r, Symbol::ADD, Precedence::ADD_SUB);
    add_rule<BinaryOpRule>(r, Symbol::SUB, Precedence::ADD_SUB);

    add_rule<BinaryOpRule>(r, Symbol::MUL, Precedence::MUL_DIV_MOD);
    add_rule<BinaryOpRule>(r, Symbol::DIV, Precedence::MUL_DIV_MOD);
    add_rule<BinaryOpRule>(r, Symbol::MOD, Precedence::MUL_DIV_MOD);

    // TODO: check that all ops have rules

    return r;
}() };

const std::map<Symbol, std::vector<std::reference_wrapper<const GrammarRule>>>
        syms_to_rules{ []() {
            std::map<
                    Symbol,
                    std::vector<std::reference_wrapper<const GrammarRule>>>
                    m;
            for (const auto& rule_ptr : grammar_rules) {
                const auto& rule = *rule_ptr;
                m[rule.op()].emplace_back(rule);
            }
            return m;
        }() };

const std::map<ListType, std::vector<std::reference_wrapper<const GrammarRule>>>
        list_types_to_implicit_rules{ []() {
            std::map<
                    ListType,
                    std::vector<std::reference_wrapper<const GrammarRule>>>
                    m;

            static const std::unique_ptr<const GrammarRule> bind_rule =
                    std::make_unique<BindRule>();
            m[ListType::PAREN].emplace_back(*bind_rule);

            static const std::unique_ptr<const GrammarRule> array_rule =
                    std::make_unique<ArrayRule>();
            m[ListType::SQUARE].emplace_back(*array_rule);

            static const std::unique_ptr<const GrammarRule> record_rule =
                    std::make_unique<RecordRule>();
            m[ListType::CURLY].emplace_back(*record_rule);
            static const std::unique_ptr<const GrammarRule> func_rule =
                    std::make_unique<FuncRule>();
            m[ListType::CURLY].emplace_back(*func_rule);
            return m;
        }() };

} // anonymous namespace

const std::vector<std::reference_wrapper<const GrammarRule>>& sym_to_rules(
        const Symbol sym)
{
    try {
        return syms_to_rules.at(sym);
    } catch (const std::out_of_range&) {
        throw InvariantViolation(
                "Lookup failed in sym_to_rules(Symbol::"
                + std::string{ sym_to_debug_str(sym) } + ")");
    }
}

const std::vector<std::reference_wrapper<const GrammarRule>>&
list_type_to_implicit_rules(const ListType list_type)
{
    try {
        return list_types_to_implicit_rules.at(list_type);
    } catch (const std::out_of_range&) {
        throw InvariantViolation(
                "Lookup failed in list_type_to_implicit_rules(ListType::"
                + std::string{ list_type_to_debug_str(list_type) } + ")");
    }
}

bool sym_is_always_binary_op(Symbol sym)
{
    const auto& rules = sym_to_rules(sym);
    for (const auto& rule : rules) {
        if (rule.get().arity() != Arity::INFIX_BINARY) {
            return false;
        }
    }
    return true;
}

} // namespace openzl::sddl
