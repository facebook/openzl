// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl2/compiler/parser/Grammar.h"

#include <sstream>
#include <string>

#include "openzl/shared/portability.h"

#include "tools/sddl2/compiler/Exception.h"
#include "tools/sddl2/compiler/Utils.h"

using namespace openzl::sddl2::detail;

namespace openzl::sddl2 {

namespace {

using ArgsVec = std::vector<ASTPtr>;

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

    { Precedence::BIT_AND, LTR },     { Precedence::BIT_XOR, LTR },
    { Precedence::BIT_OR, LTR },

    { Precedence::LOG_AND, LTR },     { Precedence::LOG_OR, LTR },

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
    { Precedence::BIT_AND,
      "BIT_AND(" + enum_to_str_of_int(Precedence::BIT_AND) + ")" },
    { Precedence::BIT_XOR,
      "BIT_XOR(" + enum_to_str_of_int(Precedence::BIT_XOR) + ")" },
    { Precedence::BIT_OR,
      "BIT_OR(" + enum_to_str_of_int(Precedence::BIT_OR) + ")" },
    { Precedence::LOG_AND,
      "LOG_AND(" + enum_to_str_of_int(Precedence::LOG_AND) + ")" },
    { Precedence::LOG_OR,
      "LOG_OR(" + enum_to_str_of_int(Precedence::LOG_OR) + ")" },
    { Precedence::ASSIGNMENT,
      "ASSIGNMENT(" + enum_to_str_of_int(Precedence::ASSIGNMENT) + ")" },
};

const std::map<Associativity, poly::string_view> associativities_to_strs{
    { Associativity::LEFT_TO_RIGHT, "LEFT_TO_RIGHT" },
    { Associativity::RIGHT_TO_LEFT, "RIGHT_TO_LEFT" },
};

const std::map<ArgType, poly::string_view> arg_types_to_strs{
    { ArgType::SYM, "SYM" },
    { ArgType::LIST_PAREN, "LIST_PAREN" },
    { ArgType::LIST_SQUARE, "LIST_SQUARE" },
    { ArgType::LIST_CURLY, "LIST_CURLY" },
    { ArgType::EXPR, "EXPR" },
};

Associativity associativity_of(Precedence precedence)
{
    try {
        return associativities.at(precedence);
    } catch (const std::out_of_range&) {
        throw InvariantViolation(
                "Lookup failed in associativity_of(Precendence::"
                + std::string{ precedence_to_str(precedence) } + ")");
    }
}

const std::vector<Symbol> builtin_field_syms{
    Symbol::BYTE,  Symbol::U8,     Symbol::I8,     Symbol::U16LE, Symbol::U16BE,
    Symbol::I16LE, Symbol::I16BE,  Symbol::U32LE,  Symbol::U32BE, Symbol::I32LE,
    Symbol::I32BE, Symbol::U64LE,  Symbol::U64BE,  Symbol::I64LE, Symbol::I64BE,
    Symbol::F16LE, Symbol::F16BE,  Symbol::F32LE,  Symbol::F32BE, Symbol::F64LE,
    Symbol::F64BE, Symbol::BF16LE, Symbol::BF16BE,
};

const std::map<Symbol, Op> syms_to_ops{
    { Symbol::EXPECT, Op::EXPECT },   { Symbol::ASSIGN, Op::ASSIGN },
    { Symbol::SIZEOF, Op::SIZEOF },   { Symbol::ASSUME, Op::ASSUME },
    { Symbol::MEMBER, Op::MEMBER },

    { Symbol::EQ, Op::EQ },           { Symbol::NE, Op::NE },

    { Symbol::ADD, Op::ADD },         { Symbol::SUB, Op::SUB },
    { Symbol::MUL, Op::MUL },         { Symbol::DIV, Op::DIV },
    { Symbol::MOD, Op::MOD },

    { Symbol::GT, Op::GT },           { Symbol::GE, Op::GE },
    { Symbol::LT, Op::LT },           { Symbol::LE, Op::LE },

    { Symbol::LOG_AND, Op::LOG_AND }, { Symbol::LOG_OR, Op::LOG_OR },
    { Symbol::LOG_NOT, Op::LOG_NOT },

    { Symbol::BIT_AND, Op::BIT_AND }, { Symbol::BIT_XOR, Op::BIT_XOR },
    { Symbol::BIT_OR, Op::BIT_OR },   { Symbol::BIT_NOT, Op::BIT_NOT },
};

} // anonymous namespace

poly::string_view precedence_to_str(Precedence precedence)
{
    try {
        return precedences_to_strs.at(precedence);
    } catch (const std::out_of_range&) {
        throw InvariantViolation("Lookup failed in precedence_to_str()");
    }
}

poly::string_view associativity_to_str(Associativity associativity)
{
    try {
        return associativities_to_strs.at(associativity);
    } catch (const std::out_of_range&) {
        throw InvariantViolation("Lookup failed in associativity_to_str()");
    }
}

poly::string_view arg_type_to_str(ArgType arg_type)
{
    try {
        return arg_types_to_strs.at(arg_type);
    } catch (const std::out_of_range&) {
        throw InvariantViolation("Lookup failed in arg_type_to_str()");
    }
}

GrammarRule::GrammarRule(
        Symbol sym,
        Precedence precedence,
        std::vector<ArgType> arg_types,
        bool has_lhs_arg)
        : sym_(sym),
          precedence_(precedence),
          associativity_(associativity_of(precedence)),
          arg_types_(std::move(arg_types)),
          has_lhs_arg_(has_lhs_arg)
{
}

Symbol GrammarRule::sym() const
{
    return sym_;
}

Precedence GrammarRule::precedence() const
{
    return precedence_;
}

Associativity GrammarRule::associativity() const
{
    return associativity_;
}

std::vector<ArgType> GrammarRule::arg_types() const&
{
    return arg_types_;
}

size_t GrammarRule::num_args() const
{
    return arg_types_.size();
}

size_t GrammarRule::num_rhs_args() const
{
    return num_args() - (has_lhs_arg_ ? 1 : 0);
}

bool GrammarRule::has_lhs_arg() const
{
    return has_lhs_arg_;
}

poly::string_view GrammarRule::sym_str() const
{
    return sym_to_debug_str(sym());
}

poly::string_view GrammarRule::precedence_str() const
{
    return precedence_to_str(precedence());
}

poly::string_view GrammarRule::associativity_str() const
{
    return associativity_to_str(associativity());
}

std::string GrammarRule::arg_types_str() const
{
    std::stringstream ss;
    ss << "[";
    for (const auto& arg_type : arg_types()) {
        ss << arg_type_to_str(arg_type) << ", ";
    }
    ss << "]";
    return std::move(ss).str();
}

std::string GrammarRule::info_str() const
{
    std::stringstream ss;
    ss << "GrammarRule(";
    ss << "Symbol::" << sym_str() << ", ";
    ss << "Precedence::" << precedence_str() << ", ";
    ss << "Associativity::" << associativity_str() << ", ";
    ss << "ArgTypes: " << arg_types_str();
    ss << ")";
    return std::move(ss).str();
}

ASTPtr GrammarRule::gen(ASTPtr op, ArgsVec args) const
{
    if (args.size() != arg_types().size()) {
        throw InvariantViolation(
                some(op).loc(),
                "Expected " + std::to_string(arg_types().size())
                        + " arguments, but got " + std::to_string(args.size())
                        + "!");
    }

    for (size_t i = 0; i < args.size(); ++i) {
        auto& arg      = args.at(i);
        auto maybe_arg = match_arg(op, std::move(arg), i);
        if (!maybe_arg) {
            throw InvariantViolation(
                    some(op).loc(),
                    "Argument failed to match while the op was being generated, i.e., after it should already have successfully been matched!");
        }
        args.at(i) = std::move(maybe_arg).value();
    }
    auto result = do_gen(std::move(op), std::move(args));
    some(result);
    return result;
}

poly::optional<ASTPtr>
GrammarRule::match_arg(const ASTPtr& op, ASTPtr arg, size_t idx) const
{
    const auto type = arg_types().at(idx);
    switch (type) {
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
        case ArgType::SYM: {
            if (some(arg).as_sym() == nullptr) {
                return poly::nullopt;
            }
            break;
        }
        default:
            throw InvariantViolation(
                    some(op).loc() + some(arg).loc(), "Illegal ArgType!");
    }

    return do_match_arg(op, std::move(arg), idx);
}

poly::optional<ASTPtr>
GrammarRule::do_match_arg(const ASTPtr&, ASTPtr arg, size_t) const
{
    return std::move(arg);
}

namespace {

class BuiltInFieldRule : public GrammarRule {
   public:
    explicit BuiltInFieldRule(Symbol sym)
            : GrammarRule(sym, Precedence::NULLARY, std::vector<ArgType>())
    {
    }

   private:
    ASTPtr do_gen(ASTPtr op, ArgsVec) const override
    {
        auto token = token_of(op);
        return std::make_shared<ASTBuiltinField>(token.loc(), token.sym());
    }
};

class OpRule : public GrammarRule {
   public:
    template <typename... Args>
    explicit OpRule(Args&&... args) : GrammarRule(std::forward<Args>(args)...)
    {
    }

   protected:
    ASTPtr do_gen(ASTPtr op, ArgsVec args) const override
    {
        auto token = op->as_sym()
                ? token_of(op)
                : Token{ SourceLocation::null(), this->sym() };

        return std::make_shared<ASTOp>(
                token.loc(), syms_to_ops.at(token.sym()), std::move(args));
    }
};

class UnaryOpRule : public OpRule {
   public:
    explicit UnaryOpRule(Symbol op, Precedence precedence = Precedence::UNARY)
            : OpRule(op, precedence, std::vector<ArgType>({ ArgType::EXPR }))
    {
    }
};

class BinaryOpRule : public OpRule {
   public:
    explicit BinaryOpRule(Symbol op, Precedence precedence)
            : OpRule(op,
                     precedence,
                     std::vector<ArgType>({ ArgType::EXPR, ArgType::EXPR }),
                     true)
    {
    }
};

class UnaryAssumeRule : public UnaryOpRule {
   public:
    explicit UnaryAssumeRule()
            : UnaryOpRule(Symbol::ASSUME, Precedence::ASSIGNMENT)
    {
    }

    ASTPtr do_gen(ASTPtr op, ArgsVec args) const override
    {
        return std::make_shared<ASTOp>(
                some(op).loc(), Op::CONSUME, std::move(args));
    }
};

class NegationRule : public UnaryOpRule {
   public:
    explicit NegationRule() : UnaryOpRule(Symbol::SUB, Precedence::UNARY) {}

    ASTPtr do_gen(ASTPtr op, ArgsVec args) const override
    {
        return std::make_shared<ASTOp>(
                some(op).loc(), Op::NEG, std::move(args));
    }
};

class ArrayRule : public GrammarRule {
   public:
    explicit ArrayRule()
            : GrammarRule(
                      Symbol::PAREN_OPEN, /* bit weird */
                      Precedence::ACCESS,
                      std::vector<ArgType>({ ArgType::EXPR }),
                      true)
    {
    }

   private:
    ASTPtr do_gen(ASTPtr op, ArgsVec args) const override
    {
        auto& type      = args.at(0);
        auto& len_nodes = unwrap_square(op);

        if (len_nodes.size() == 0) {
            return std::make_shared<ASTArray>(std::move(type));
        }

        if (len_nodes.size() != 1) {
            throw ParseError(
                    some(op).loc(),
                    "Array declaration square must have 0-1 elements.");
        }

        return std::make_shared<ASTArray>(std::move(type), len_nodes.at(0));
    }
};

class RecordRule : public GrammarRule {
   public:
    explicit RecordRule()
            : GrammarRule(
                      Symbol::RECORD,
                      Precedence::NULLARY,
                      std::vector<ArgType>({ ArgType::EXPR,
                                             ArgType::LIST_PAREN,
                                             ArgType::SYM,
                                             ArgType::LIST_CURLY }))
    {
    }

   private:
    ASTPtr do_gen(ASTPtr op, ArgsVec args) const override
    {
        auto& name   = args.at(0);
        auto& params = args.at(1);
        auto& assign = args.at(2);
        auto& fields = args.at(3);

        if (some(assign) != Symbol::ASSIGN) {
            throw InvariantViolation(
                    some(op).loc(),
                    "Expected assignment operator in record declaration!");
        }

        return Codegen{ some(op).loc() }.assign(
                std::move(name),
                std::make_shared<ASTRecord>(
                        std::move(params), std::move(fields)));
    }
};

class AnonymousRecordRule : public GrammarRule {
   public:
    explicit AnonymousRecordRule()
            : GrammarRule(
                      Symbol::RECORD,
                      Precedence::NULLARY,
                      std::vector<ArgType>(
                              { ArgType::LIST_PAREN, ArgType::LIST_CURLY }))
    {
    }

   private:
    ASTPtr do_gen(ASTPtr op, ArgsVec args) const override
    {
        auto& params = args.at(0);
        auto& fields = args.at(1);

        return std::make_shared<ASTRecord>(
                std::move(params), std::move(fields));
    }
};

class CallRule : public GrammarRule {
   public:
    explicit CallRule()
            : GrammarRule(
                      Symbol::PAREN_OPEN, /* bit weird */
                      Precedence::ACCESS,
                      std::vector<ArgType>({ ArgType::EXPR }),
                      true)
    {
    }

   private:
    ASTPtr do_gen(ASTPtr op, ArgsVec args) const override
    {
        auto& target     = args.at(0);
        const auto* list = some(op).as_list();
        if (list == nullptr || list->list_type() != ListType::PAREN) {
            throw InvariantViolation(
                    some(op).loc(), "Expected paren list in call.");
        }
        return std::make_shared<ASTCall>(
                std::move(target), unwrap_parens(list->nodes()));
    }
};

class BytesRule : public GrammarRule {
   public:
    explicit BytesRule()
            : GrammarRule(
                      Symbol::BYTES,
                      Precedence::NULLARY,
                      std::vector<ArgType>({ ArgType::LIST_PAREN }))
    {
    }

   private:
    ASTPtr do_gen(ASTPtr op, ArgsVec args) const override
    {
        return std::make_shared<ASTBytes>(op->loc(), args.at(0));
    }
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

    // Built-in fields
    for (const auto& sym : builtin_field_syms) {
        add_rule<BuiltInFieldRule>(r, sym);
    }

    // Complex fields
    add_rule<RecordRule>(r);
    add_rule<AnonymousRecordRule>(r);
    add_rule<BytesRule>(r);

    // Ops
    add_rule<UnaryOpRule>(r, Symbol::EXPECT, Precedence::ASSIGNMENT);
    add_rule<UnaryOpRule>(r, Symbol::SIZEOF);

    add_rule<BinaryOpRule>(r, Symbol::ASSIGN, Precedence::ASSIGNMENT);
    add_rule<BinaryOpRule>(r, Symbol::ASSUME, Precedence::ASSIGNMENT);
    add_rule<UnaryAssumeRule>(r);
    add_rule<BinaryOpRule>(r, Symbol::MEMBER, Precedence::ACCESS);

    add_rule<BinaryOpRule>(r, Symbol::EQ, Precedence::EQUALITY);
    add_rule<BinaryOpRule>(r, Symbol::NE, Precedence::EQUALITY);

    add_rule<BinaryOpRule>(r, Symbol::GT, Precedence::RELATION);
    add_rule<BinaryOpRule>(r, Symbol::GE, Precedence::RELATION);
    add_rule<BinaryOpRule>(r, Symbol::LT, Precedence::RELATION);
    add_rule<BinaryOpRule>(r, Symbol::LE, Precedence::RELATION);

    add_rule<BinaryOpRule>(r, Symbol::ADD, Precedence::ADD_SUB);
    add_rule<BinaryOpRule>(r, Symbol::SUB, Precedence::ADD_SUB);
    add_rule<NegationRule>(r);

    add_rule<BinaryOpRule>(r, Symbol::MUL, Precedence::MUL_DIV_MOD);
    add_rule<BinaryOpRule>(r, Symbol::DIV, Precedence::MUL_DIV_MOD);
    add_rule<BinaryOpRule>(r, Symbol::MOD, Precedence::MUL_DIV_MOD);

    add_rule<BinaryOpRule>(r, Symbol::BIT_AND, Precedence::BIT_AND);
    add_rule<BinaryOpRule>(r, Symbol::BIT_OR, Precedence::BIT_OR);
    add_rule<BinaryOpRule>(r, Symbol::BIT_XOR, Precedence::BIT_XOR);
    add_rule<UnaryOpRule>(r, Symbol::BIT_NOT);

    add_rule<BinaryOpRule>(r, Symbol::LOG_AND, Precedence::LOG_AND);
    add_rule<BinaryOpRule>(r, Symbol::LOG_OR, Precedence::LOG_OR);
    add_rule<UnaryOpRule>(r, Symbol::LOG_NOT);

    // TODO: check that all ops have rules

    return r;
}() };

using GrammarRuleRefs = std::vector<std::reference_wrapper<const GrammarRule>>;

const std::map<Symbol, GrammarRuleRefs> syms_to_rules{ []() {
    std::map<Symbol, GrammarRuleRefs> m;
    for (const auto& rule_ptr : grammar_rules) {
        const auto& rule = *rule_ptr;
        m[rule.sym()].emplace_back(rule);
    }
    return m;
}() };

const std::map<ListType, GrammarRuleRefs> list_types_to_rules{ []() {
    std::map<ListType, GrammarRuleRefs> m;

    static const std::unique_ptr<const GrammarRule> call_rule =
            std::make_unique<CallRule>();
    m[ListType::PAREN] = { *call_rule };

    static const std::unique_ptr<const GrammarRule> array_rule =
            std::make_unique<ArrayRule>();
    m[ListType::SQUARE] = { *array_rule };
    m[ListType::CURLY]  = {};
    return m;
}() };

} // anonymous namespace

const GrammarRuleRefs& sym_to_rules(const Symbol sym)
{
    try {
        return syms_to_rules.at(sym);
    } catch (const std::out_of_range&) {
        throw InvariantViolation(
                "Lookup failed in sym_to_rules(Symbol::"
                + std::string{ sym_to_debug_str(sym) } + ")");
    }
}

const GrammarRuleRefs& list_type_to_rules(const ListType list_type)
{
    try {
        return list_types_to_rules.at(list_type);
    } catch (const std::out_of_range&) {
        throw InvariantViolation(
                "Lookup failed in list_type_to_rules(ListType::"
                + std::string{ list_type_to_debug_str(list_type) } + ")");
    }
}
} // namespace openzl::sddl2
