// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl2/compiler/parser/Parser.h"

#include <algorithm>
#include <list>

#include "tools/sddl2/compiler/Exception.h"
#include "tools/sddl2/compiler/Utils.h"
#include "tools/sddl2/compiler/parser/Grammar.h"

using namespace openzl::sddl2::detail;

namespace openzl::sddl2 {

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

struct PendingOp {
    PendingOp(
            const std::reference_wrapper<const GrammarRule>& rule,
            size_t pos,
            std::list<ASTPtr>::iterator iter)
            : iter_(iter), pos_(pos), rule_(rule)
    {
    }

    std::list<ASTPtr>::iterator iter_;
    size_t pos_;
    std::reference_wrapper<const GrammarRule> rule_;

    bool operator<(const PendingOp& other) const
    {
        auto const lprecedence = rule_.get().precedence();
        auto const rprecedence = other.rule_.get().precedence();

        if (lprecedence != rprecedence) {
            return lprecedence < rprecedence;
        };

        auto const lassoc = rule_.get().associativity();
        auto const rassoc = other.rule_.get().associativity();
        if (lassoc != rassoc) {
            throw InvariantViolation(
                    maybe_loc(*this->iter_) + maybe_loc(*other.iter_),
                    "Two symbols ('"
                            + std::string{ sym_to_debug_str(
                                    (this->rule_.get().sym())) }
                            + "' and '"
                            + std::string{ sym_to_debug_str(
                                    other.rule_.get().sym()) }
                            + "') with the same precedence can't have different associativities.");
        }

        switch (lassoc) {
            case Associativity::LEFT_TO_RIGHT:
                return this->pos_ < other.pos_;
            case Associativity::RIGHT_TO_LEFT:
                return this->pos_ > other.pos_;
            default:
                throw InvariantViolation("Illegal associativity!");
        }
    }
};

/**
 * Remove all ops from the list that are associated with the given iterator.
 */
void remove_from_ops(
        std::list<PendingOp>& ops,
        const std::list<ASTPtr>::iterator& iter)
{
    ops.erase(
            std::remove_if(
                    ops.begin(),
                    ops.end(),
                    [&iter](const PendingOp& op) { return op.iter_ == iter; }),
            ops.end());
}

class ParserImpl {
   public:
    explicit ParserImpl(const Logger& logger) : log_(logger) {}

   private:
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
        const auto all_nodes_loc = join_locs(expr_group.nodes());
        log_(3) << InfoError(all_nodes_loc, "Parsing expression:").what();

        // Parse sub-expressions
        std::list<ASTPtr> nodes{};
        for (const auto& group_node : expr_group.nodes()) {
            nodes.push_back(parse_node(
                    some(group_node), GroupType::TOKEN | GroupType::LIST));
        }

        // Check for empty top-level expression
        if (nodes.size() == 0) {
            const auto& full_loc = expr_group.loc();
            throw ParseError(full_loc, "Empty expression.");
        }

        // Find all the symbols and their rules
        std::list<PendingOp> pending_ops{};

        size_t pos = 0;
        for (auto it = nodes.begin(); it != nodes.end(); ++it, pos++) {
            auto sym  = (*it)->as_sym();
            auto list = (*it)->as_list();
            if (sym) {
                for (const auto& rule : sym_to_rules(**sym)) {
                    pending_ops.emplace_back(PendingOp(rule, pos, it));
                }
            }
            if (list) {
                for (const auto& rule : list_type_to_rules(list->list_type())) {
                    pending_ops.emplace_back(rule, pos, it);
                }
            }
        }
        pending_ops.sort();

        // Helper to throw or log an error message depending on the
        // state of throw_on_failure_to_match.
        bool throw_on_failure_to_match = false;
        auto throw_or_log              = [&](std::string msg) -> void {
            if (throw_on_failure_to_match) {
                throw ParseError(all_nodes_loc, msg);
            }
            log_(3) << InfoError(all_nodes_loc, msg).what();
        };

        // Apply the rules
        while (!pending_ops.empty()) {
            bool changed = false;

            log_(3) << "Current Nodes:" << std::endl;
            for (const auto& node : nodes) {
                node->print(log_(3), 4);
            }

            for (const auto& op : pending_ops) {
                {
                    auto& rule    = op.rule_.get();
                    auto iter     = op.iter_;
                    auto num_args = rule.num_args();
                    auto num_rhs  = rule.num_rhs_args();

                    log_(3) << InfoError(
                                       (*iter)->loc(),
                                       "Considering rule:\n  "
                                               + rule.info_str())
                                       .what();

                    std::vector<std::list<ASTPtr>::iterator> arg_iters;

                    // Get the lhs arg (if any)
                    if (rule.has_lhs_arg()) {
                        if (iter == nodes.begin()) {
                            throw_or_log(
                                    "Rule requires a lhs arg but none found.");
                            goto next_op;
                        }
                        arg_iters.push_back(std::prev(iter));
                    }

                    // Get the rhs args
                    if (num_rhs) {
                        if (num_args == 1 && iter != nodes.begin()) {
                            // For prefix unary operators: if there's a valid
                            // expression to the left, skip this unary rule
                            // in favor of a binary rule.
                            if ((*std::prev(iter))->as_sym() == nullptr) {
                                throw_or_log(
                                        "Skipping prefix unary rule because there's a "
                                        "valid lhs expression.");
                                goto next_op;
                            }
                        }
                        auto arg_iter = iter;
                        for (size_t i = 0; i < num_rhs; ++i) {
                            arg_iter = std::next(arg_iter);
                            if (arg_iter == nodes.end()) {
                                throw_or_log(
                                        "Rule requires "
                                        + std::to_string(num_rhs)
                                        + " rhs args but " + std::to_string(i)
                                        + " found.");
                                goto next_op;
                            }
                            arg_iters.push_back(arg_iter);
                        }
                    }

                    for (size_t i = 0; i < num_args; ++i) {
                        auto arg = rule.match_arg(*iter, *arg_iters.at(i), i);
                        if (!arg) {
                            throw_or_log(
                                    "Failed to match args for rule: "
                                    + rule.info_str());
                            goto next_op;
                        }
                        *arg_iters.at(i) = *std::move(arg);
                    }

                    log_(3) << "Applying rule." << std::endl;
                    std::vector<ASTPtr> args(num_args);
                    for (size_t i = 0; i < num_args; i++) {
                        args.at(i) = *arg_iters.at(i);
                    }
                    auto result = rule.gen(*iter, std::move(args));

                    for (auto& arg_iter : arg_iters) {
                        remove_from_ops(pending_ops, arg_iter);
                        nodes.erase(arg_iter);
                    }
                    remove_from_ops(pending_ops, iter);
                    *iter   = result;
                    changed = true;
                    break;
                }

            next_op:;
            }
            // Stopped making forward progress.
            throw_on_failure_to_match = changed == false;
        }
        if (nodes.size() != 1) {
            throw InvariantViolation(
                    all_nodes_loc, "Expected operator between expressions.");
        }
        return nodes.front();
    }

    ASTPtr parse_token(const Token& token) const
    {
        return token.visit([&token](const auto& val) -> ASTPtr {
            using T = std::decay_t<decltype(val)>;
            if constexpr (std::is_same_v<T, Symbol>) {
                return std::make_shared<ASTSym>(token);
            } else if constexpr (std::is_same_v<T, poly::string_view>) {
                // All string tokens are treated as identifiers. All
                // operators and keywords have already been parsed out.
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
} // namespace openzl::sddl2
