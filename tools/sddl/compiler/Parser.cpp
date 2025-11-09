// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl/compiler/Parser.h"

#include <algorithm>
#include <list>
#include <set>
#include <type_traits>

#include "openzl/shared/portability.h"

#include "tools/sddl/compiler/Exception.h"
#include "tools/sddl/compiler/Grammar.h"
#include "tools/sddl/compiler/Utils.h"

using namespace openzl::sddl::detail;

namespace openzl::sddl {

namespace {

enum class GroupType : uint32_t {
    TOKEN = 1,
    LIST  = 2,
    EXPR  = 4,
};

GroupType operator|(const GroupType& a, const GroupType& b)
{
    return static_cast<GroupType>(
            static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}

GroupType operator&(const GroupType& a, const GroupType& b)
{
    return static_cast<GroupType>(
            static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
}

bool operator==(const GroupType& a, const uint32_t& b)
{
    return static_cast<uint32_t>(a) == b;
}

class ParserImpl {
   public:
    explicit ParserImpl(const Logger& logger) : log_(logger) {}

   private:
    struct PendingOp {
        PendingOp(
                std::list<ASTPtr>::iterator _node_it,
                size_t _pos,
                const GrammarRule& _rule)
                : node_it(_node_it), pos(_pos), rule(_rule)
        {
        }

        void print(std::ostream& os, size_t indent = 0) const
        {
            os << std::string(indent, ' ') << "PendingOp(\n";
            os << std::string(indent + 2, ' ') << "AST Node:\n";
            some(*node_it).print(os, indent + 4);
            os << std::string(indent + 2, ' ') << "AST Pos: " << pos << ":\n";
            const auto& loc = some(*node_it).loc();
            os << std::string(indent + 4, ' ') << loc.pos_str() << "\n";
            os << loc.contents_str(indent + 4);
            os << std::string(indent + 2, ' ') << "Rule:\n";
            os << std::string(indent + 4, ' ') << rule.get().info_str() << "\n";
            os << std::string(indent, ' ') << ")\n";
        }

        std::list<ASTPtr>::iterator node_it;
        size_t pos;
        std::reference_wrapper<const GrammarRule> rule;
    };

    void check_partially_parsed_expr_has_no_unmergeable_adjacent_exprs(
            const SourceLocation& full_loc,
            const std::list<ASTPtr>& nodes) const
    {
        auto it1       = nodes.cbegin();
        auto it2       = nodes.cbegin();
        const auto end = nodes.cend();

        if (it1 == nodes.end()) {
            throw InvariantViolation(full_loc, "Empty expression!?");
        }

        ++it2;
        while (it2 != end) {
            const auto& lhs = some(*it1);
            const auto& rhs = some(*it2);

            const auto lhs_is_sym  = lhs.as_sym() != nullptr;
            const auto rhs_is_sym  = rhs.as_sym() != nullptr;
            const auto rhs_is_list = rhs.as_list() != nullptr;

            if (!lhs_is_sym && !rhs_is_sym && !rhs_is_list) {
                // TODO: actually consult implicit operators list?
                throw ParseError(
                        lhs.loc() + rhs.loc(),
                        "Expected operator between expressions.");
            }

            if (lhs_is_sym && rhs_is_sym) {
                const auto lhs_sym = **lhs.as_sym();
                const auto rhs_sym = **rhs.as_sym();

                if (sym_is_always_binary_op(lhs_sym)
                    && sym_is_always_binary_op(rhs_sym)) {
                    throw ParseError(
                            lhs.loc() + rhs.loc(),
                            "Expected expression between operators.");
                }
            }

            it1 = it2;
            ++it2;
        }
    }

    poly::optional<Arity> resolve_arity(
            const SourceLocation& loc,
            const std::vector<std::reference_wrapper<const GrammarRule>>& rules,
            const GrammarRule& rule,
            const ASTPtr& lhs) const
    {
        const auto arity = rule.arity();

        if (rules.size() == 1) {
            if (&rules[0].get() != &rule) {
                throw InvariantViolation(
                        loc,
                        "Processing a rule not in the list of rules for that op!");
            }
            return arity;
        }

        if (rules.size() != 2) {
            throw InvariantViolation(loc, "More than two rules!");
        }

        std::set<Arity> arities;
        for (const auto& r : rules) {
            arities.insert(r.get().arity());
        }

        if (arities
            != std::set<Arity>{
                    { Arity::PREFIX_UNARY, Arity::INFIX_BINARY } }) {
            throw InvariantViolation(
                    loc,
                    "Can only handle operators with more than one interpretation when the possible interpretations are (1) prefix-unary or (2) infix-binary!");
        }

        if (lhs == nullptr) {
            return Arity::PREFIX_UNARY;
        }

        if (lhs->as_sym() == nullptr) {
            return Arity::INFIX_BINARY;
        }

        const auto& lhs_sym     = *lhs->as_sym();
        const auto lhs_sym_type = sym_type(*lhs_sym);

        if (lhs_sym_type == SymbolType::OPERATOR) {
            // TODO: handle postfix-unary ops if/when they're
            // added.
            // const auto& lhs_rules = sym_to_rules(*lhs_sym);
            return Arity::PREFIX_UNARY;
        } else {
            // This... shouldn't happen?
            return poly::nullopt;
        }
    }

    /**
     * The goal of this method is to take a list of nodes we got from the
     * grouping stage (i.e., a mostly-flat list of tokens, with the exception
     * of lists, whose elements are wrapped into a single top-level item) and
     * progressively merge operators and their adjacent argument expression(s)
     * to turn that flat list into an expression tree with a single root.
     *
     * This is notionally simple in the case of unambiguous arities of
     * operators. We follow the following algorithm:
     *
     * - Parse all group nodes into sub-expressions;
     * - Find all operators;
     * - Get all conversion rules for those operators;
     * - Sort all operator & rule pairs by precedence and associativity;
     * - While unconverted operators remain:
     *   - For each conversion rule in sorted order:
     *     - If arguments are compatible with the operator:
     *       - Generate an ASTNode from the operator and its args
     *       - Break out of the inner loop and return to the top of the
     *         outer loop.
     *   - If we don't find any collapsible operators, throw an error.
     *
     * An additional complexity arises in the case of operators
     * which can either be unary or binary (i.e., `+` and `-`). It's tricky
     * because the unary versions of those operators actually have higher
     * precedence than their binary equivalents. So if you didn't otherwise
     * impose a control to subvert the purely precedence and associativity
     * based parse of the expression, you would always select the unary version
     * and then the parse would fail if the user actually wanted the binary
     * operators.
     *
     * So we modify the application of operator transformation rules by
     * inspecting the expressions adjacent to the operator. Note that:
     *
     * 1. Two expressions must have an operator between them to be reduced into
     *    one expression.
     * 2. A value (a variable, a number, an expression) will never turn back
     *    into an operator, no matter what folding happens between it and other
     *    nodes on its other side.
     * 3. All unary/binary ambiguous operators are either prefix-unary or
     *    infix-binary. There are no postfix-unary operators that can also be
     *    interpreted as infix-binary.
     *
     * That means that we can apply the following rule. When looking at an
     * operator that can be interpreted either as a prefix-unary or as a
     * infix-binary operator, look at the adjacent expression on the left hand
     * side:
     *
     * - If it's an operator:
     *   - If it's a prefix op, resolve this operator as prefix-unary.
     *   - If it's a binary op, resolve this operator as prefix-unary.
     *   - If it's a postfix op, resolve this operator as infix-binary.
     * - If it's a value, resolve this operator as infix-binary.
     * - If there is no LHS, because this is the first token, resolve it as
     *   prefix-unary, duh.
     *
     * During parsing, the node list maintains the partially converted list of
     * sub-expressions. It can contain the following kinds of things:
     *
     * - Converted Exprs:
     *   - Op
     *   - Num
     *   - Field
     *   - Dest
     *   - Var
     * - Unconverted Exprs:
     *   - Symbols (specifically: operators)
     *   - Lists
     *
     * When parsing is finished, it should have resulted in a single converted
     * expression, which is then what is returned.
     */
    ASTPtr parse_expr(const GroupingExpr& expr_group) const
    {
        // Should include expression terminator
        const auto& full_loc = expr_group.loc();
        // Doesn't include expression terminator
        const auto all_nodes_loc = join_locs(expr_group.nodes());

        log_(3) << InfoError(all_nodes_loc, "Parsing expression:").what();

        std::list<ASTPtr> nodes{};
        try {
            for (const auto& group_node : expr_group.nodes()) {
                nodes.push_back(parse_node(
                        some(group_node), GroupType::TOKEN | GroupType::LIST));
            }

            if (nodes.size() == 0) {
                throw ParseError(full_loc, "Empty expression.");
            }

            std::vector<PendingOp> sorted_pending_ops;
            size_t i = 0;
            for (auto it = nodes.begin(); it != nodes.end(); ++it) {
                const auto ast_sym  = some(*it).as_sym();
                const auto ast_list = some(*it).as_list();
                if (ast_sym != nullptr) {
                    const auto sym    = **ast_sym;
                    const auto& rules = sym_to_rules(sym);
                    if (rules.empty()) {
                        throw ParseError(
                                (*it)->loc(),
                                "Symbol '"
                                        + std::string{ sym_to_debug_str(sym) }
                                        + "' has no associated grammar rules.");
                    }
                    for (const auto& rule : rules) {
                        // log_(3) << InfoError("Adding rule:\n" +
                        // rule.get().info_str()).what();
                        sorted_pending_ops.emplace_back(it, i, rule);
                    }
                } else if (ast_list != nullptr) {
                    const auto& implicit_rules =
                            list_type_to_implicit_rules(ast_list->list_type());
                    for (const auto& rule : implicit_rules) {
                        sorted_pending_ops.emplace_back(it, i, rule);
                    }
                }
                i++;
            }

            const auto pending_op_comparator = [](const PendingOp& lhs,
                                                  const PendingOp& rhs) {
                const auto& lrule = lhs.rule.get();
                const auto& rrule = rhs.rule.get();

                if (lrule.precedence() != rrule.precedence()) {
                    return lrule.precedence() < rrule.precedence();
                }
                if (lrule.associativity() != rrule.associativity()) {
                    throw ParseError(
                            maybe_loc(*lhs.node_it) + maybe_loc(*rhs.node_it),
                            "Two symbols ('"
                                    + std::string{ sym_to_debug_str(
                                            lrule.op()) }
                                    + "' and '"
                                    + std::string{ sym_to_debug_str(
                                            rrule.op()) }
                                    + "') with the same precedence can't have different associativities.");
                }
                const auto associativity = lrule.associativity();
                switch (associativity) {
                    case Associativity::LEFT_TO_RIGHT:
                        return lhs.pos < rhs.pos;
                    case Associativity::RIGHT_TO_LEFT:
                        return lhs.pos > rhs.pos;
                    default:
                        throw InvariantViolation("Illegal associativity!");
                }
            };

            std::sort(
                    sorted_pending_ops.begin(),
                    sorted_pending_ops.end(),
                    pending_op_comparator);

            for (const auto& pending_op : sorted_pending_ops) {
                auto& log = log_(3);
                log << "Grammar rule to consider applying to this expression:\n";
                pending_op.print(log, 2);
            }

            // In order to handle operators that can parse multiple different
            // ways, or operators whose arguments haven't yet resolved, we
            // normally allow pending ops to fail to match their arguments.
            // However, if we stop making forward progress, that means we no
            // longer have hope that future iterations of the loop will make ops
            // viable, and we can pick one to throw an error.
            bool throw_on_failure_to_match = false;

            while (true) {
                bool changed = false;

                {
                    auto& log = log_(3);
                    log << "Current top-level nodes in this pass:" << std::endl;
                    for (const auto& node : nodes) {
                        node->print(log, 2);
                    }
                }

                check_partially_parsed_expr_has_no_unmergeable_adjacent_exprs(
                        full_loc, nodes);

                for (const auto& pending_op : sorted_pending_ops) {
                    const auto& rule   = pending_op.rule.get();
                    const auto node_it = pending_op.node_it;
                    auto op            = *node_it;
                    auto next          = node_it;

                    if (op->as_sym() != nullptr) {
                        // Otherwise, it's an implicit rule and *this* is the
                        // arg.
                        ++next;
                    }

                    log_(3) << InfoError(
                                       some(op).loc(),
                                       "Considering rule:\n  "
                                               + rule.info_str())
                                       .what();

                    const auto is_first = node_it == nodes.begin();
                    const auto is_end   = next == nodes.end();

                    auto prev = node_it;
                    if (!is_first) {
                        --prev;
                    }

                    ASTPtr lhs{};
                    ASTPtr rhs{};

                    std::vector<std::reference_wrapper<const GrammarRule>>
                            all_rules_for_this_node;
                    for (const auto& potential_pending_op :
                         sorted_pending_ops) {
                        if (potential_pending_op.node_it == node_it) {
                            all_rules_for_this_node.push_back(
                                    potential_pending_op.rule);
                        }
                    }

                    const auto opt_arity = resolve_arity(
                            some(op).loc(),
                            all_rules_for_this_node,
                            rule,
                            is_first ? lhs : *prev);

                    if (!opt_arity.has_value()) {
                        continue;
                    }

                    if (opt_arity.value() != rule.arity()) {
                        continue;
                    }

                    ASTPtr result;
                    switch (rule.arity()) {
                        case Arity::INFIX_BINARY: {
                            if (is_first) {
                                if (throw_on_failure_to_match) {
                                    throw ParseError(
                                            some(op).loc(),
                                            "Operator missing left-hand argument.");
                                }
                                // Can't collapse infix op with no lhs expr!
                                continue;
                            }
                            auto maybe_lhs = rule.match_lhs(op, *prev);
                            if (!maybe_lhs) {
                                continue;
                            }
                            lhs = std::move(*maybe_lhs);
                            if (lhs->as_sym() != nullptr) {
                                // Still a bare symbol, must be parsed into an
                                // expression before it can be an argument.
                                continue;
                            }
                            ZL_FALLTHROUGH;
                        }
                        case Arity::PREFIX_UNARY: {
                            if (is_end) {
                                if (throw_on_failure_to_match) {
                                    throw ParseError(
                                            some(op).loc(),
                                            "Operator missing right-hand argument.");
                                }
                                // Can't collapse arg-taking op with no rhs
                                // expr!
                                continue;
                            }
                            auto maybe_rhs = rule.match_rhs(op, *next);
                            if (!maybe_rhs) {
                                continue;
                            }
                            rhs = std::move(*maybe_rhs);
                            if (rhs->as_sym() != nullptr) {
                                // Still a bare symbol, must be parsed into an
                                // expression before it can be an argument.
                                continue;
                            }
                            ZL_FALLTHROUGH;
                        }
                        case Arity::NULLARY: {
                            auto& log = log_(3);
                            log << InfoError(
                                           maybe_loc(op),
                                           "Applying rule:\n  "
                                                   + rule.info_str())
                                            .what();
                            pending_op.print(log, 2);
                            if (lhs) {
                                log << InfoError(maybe_loc(lhs), "With lhs:")
                                                .what();
                                lhs->print(log, 2);
                            }
                            if (rhs) {
                                log << InfoError(maybe_loc(rhs), "With rhs:")
                                                .what();
                                rhs->print(log, 2);
                            }
                        }
                            result = rule.gen(
                                    std::move(op),
                                    std::move(lhs),
                                    std::move(rhs));
                            break;
                        default:
                            throw InvariantViolation(
                                    some(op).loc(), "Illegal arity!");
                    }

                    // Erase all inserted pending ops for this symbol and for
                    // its arguments. (There might be multiple if there are
                    // multiple possible interpretations of this symbol).
                    //
                    // DANGER: Note that we are mutating the vector of pending
                    // ops while this scope holds references to stuff in that
                    // vector! All those refs are invalid once we start
                    // mutating the vector. Anything accessed after this point
                    // can't be a ref into the pending ops vector.
                    auto erase_pending_ops_for_node = [this,
                                                       &sorted_pending_ops](
                                                              const std::list<
                                                                      ASTPtr>::
                                                                      iterator&
                                                                              it) {
                        auto predicate = [this, &it](const PendingOp& pop) {
                            const auto erase = pop.node_it == it;
                            if (erase) {
                                log_(4) << InfoError(
                                                   maybe_loc(*pop.node_it),
                                                   "Erasing rule:\n  "
                                                           + pop.rule.get()
                                                                     .info_str())
                                                   .what();
                            }
                            return erase;
                        };
                        const auto new_end = std::remove_if(
                                sorted_pending_ops.begin(),
                                sorted_pending_ops.end(),
                                std::move(predicate));
                        sorted_pending_ops.erase(
                                new_end, sorted_pending_ops.end());
                    };

                    erase_pending_ops_for_node(node_it);

                    *node_it = std::move(result);

                    switch (rule.arity()) {
                        case Arity::INFIX_BINARY:
                            erase_pending_ops_for_node(prev);
                            nodes.erase(prev);
                            ZL_FALLTHROUGH;
                        case Arity::PREFIX_UNARY:
                            if (node_it != next) {
                                erase_pending_ops_for_node(next);
                                nodes.erase(next);
                            }
                            ZL_FALLTHROUGH;
                        case Arity::NULLARY:
                            break;
                        default:
                            throw InvariantViolation(
                                    some(op).loc(), "Illegal arity!");
                    }
                    changed = true;
                    log_(3) << "\n";
                    break;
                }

                if (nodes.size() == 0) {
                    throw InvariantViolation(
                            full_loc,
                            "Expression reduced to 0 nodes somehow??");
                }

                if (!changed) {
                    if (!throw_on_failure_to_match) {
                        // Run through them one more time to try to produce a
                        // more specific error message.
                        throw_on_failure_to_match = true;
                        continue;
                    }

                    if (nodes.size() > 1) {
                        for (const auto& node : nodes) {
                            log_(0) << InfoError(
                                               some(node).loc(),
                                               "Uncombined sub-expression:")
                                               .what();
                            node->print(log_(0), 0);
                        }

                        throw ParseError(
                                full_loc,
                                "Couldn't reduce expression to a single AST node.");
                    } else {
                        return std::move(*nodes.begin());
                    }
                }
            }
        } catch (const CompilerException&) {
            for (const auto& node : nodes) {
                log_(0) << InfoError(some(node).loc(), "With sub-expression:")
                                   .what();
                some(node).print(log_(0), 1);
            }
            throw;
        }
    }

    ASTPtr parse_token(const Token& token) const
    {
        return token.visit([&token](const auto& val) -> ASTPtr {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, Symbol>) {
                return std::make_shared<ASTSym>(token);
            } else if constexpr (std::is_same_v<T, poly::string_view>) {
                // All string tokens are treated as identifiers. All operators
                // and keywords have already been parsed out.
                return std::make_shared<ASTVar>(token);
            } else if constexpr (std::is_same_v<T, int64_t>) {
                return std::make_shared<ASTNum>(token);
            } else {
#if __cplusplus >= 201703L
                static_assert(dependent_false_v<T>, "Non-exhaustive visitor!");
#else
                throw InvariantViolation(
                        token.loc(), "Non-exhaustive visitor!");
#endif
            }
        });
    }

    ASTPtr parse_list(const GroupingList& list_group) const
    {
        ASTVec nodes;

        for (const auto& group_node : list_group.nodes()) {
            nodes.push_back(parse_node(some(group_node), GroupType::EXPR));
        }

        auto open  = parse_node(some(list_group.open()), GroupType::TOKEN);
        auto close = parse_node(some(list_group.close()), GroupType::TOKEN);

        return std::make_shared<ASTList>(
                list_group.type(),
                std::move(open),
                std::move(close),
                std::move(nodes));
    }

    ASTPtr parse_node(const GroupingNode& node, GroupType allowed_types) const
    {
        const auto* const token = node.as_token();
        if (token != nullptr) {
            if ((GroupType::TOKEN & allowed_types) == 0) {
                throw InvariantViolation(
                        node.loc(),
                        "Group of type 'Token' not allowed in this context.");
            }
            return parse_token(**token);
        }

        const auto* const list = node.as_list();
        if (list != nullptr) {
            if ((GroupType::LIST & allowed_types) == 0) {
                throw InvariantViolation(
                        node.loc(),
                        "Group of type 'List' not allowed in this context.");
            }
            return parse_list(*list);
        }

        const auto* const expr = node.as_expr();
        if (expr != nullptr) {
            if ((GroupType::EXPR & allowed_types) == 0) {
                throw InvariantViolation(
                        node.loc(),
                        "Group of type 'Expr' not allowed in this context.");
            }
            return parse_expr(*expr);
        }

        throw InvariantViolation(node.loc(), "Unknown grouping node type!");
    }

    ASTVec parse_stmts(const GroupingVec& groups) const
    {
        ASTVec stmts;
        for (const auto& node : groups) {
            stmts.push_back(
                    unwrap_parens(parse_node(some(node), GroupType::EXPR)));
        }
        return stmts;
    }

   public:
    ASTVec parse(const GroupingVec& groups)
    {
        const auto nodes = parse_stmts(groups);

        {
            auto& log = log_(1);
            log << "AST:" << std::endl;
            for (const auto& node : nodes) {
                node->print(log, 2);
            }
            log << std::endl;
        }

        return nodes;
    }

   private:
    const Logger& log_;
};
} // anonymous namespace

Parser::Parser(const Logger& logger) : log_(logger) {}

ASTVec Parser::parse(const GroupingVec& groups) const
{
    return ParserImpl{ log_ }.parse(groups);
}
} // namespace openzl::sddl
