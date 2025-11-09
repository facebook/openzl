// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl/compiler/Compiler.h"

#include "tools/sddl/compiler/Grouper.h"
#include "tools/sddl/compiler/Parser.h"
#include "tools/sddl/compiler/Serializer.h"
#include "tools/sddl/compiler/Source.h"
#include "tools/sddl/compiler/Tokenizer.h"

namespace openzl::sddl {

Compiler::Compiler(Options options)
        : options_(std::move(options)),
          logger_(*options_.log_os, options_.verbosity),
          tokenizer_(logger_),
          grouper_(logger_),
          parser_(logger_),
          serializer_(logger_, options_.include_debug_info)
{
}

/**
 * The compiler for SDDL is comprised of four passes:
 *
 * 1. Tokenization:
 *
 *    Converts the contiguous string of source code into a flat list of tokens.
 *    Strips whitespace and comments.
 *
 *    E.g., `arr = Array(foo, bar + 1); consume arr;` ->
 *    ```
 *    [
 *      Word("arr"), Symbol::ASSIGN, Symbol::ARRAY, Symbol::PAREN_OPEN,
 *      Word("foo"), Symbol::COMMA, Word("bar"), Symbol::ADD, Num(1),
 *      Symbol::PAREN_CLOSE, Symbol::SEMI, Symbol::CONSUME, Word("arr"),
 *      Symbol::SEMI,
 *    ]
 *    ```
 *
 * 2. Grouping:
 *
 *    Breaks the flat list of tokens into explicitly separated groups of tokens.
 *    Removes all separator tokens from the token stream.
 *
 *    a) Splits the top level stream into statements based on the statement
 *       separator.
 *    b) Groups list expressions (parentheses, etc.) into a list node with an
 *       expression for each element.
 *
 *    E.g., the token list from above would become approximately:
 *
 *    ```
 *    [
 *      Expr([
 *        Word("arr"), Symbol::ASSIGN, Symbol::ARRAY,
 *        List(PAREN, [
 *          Expr([Word("foo")]),
 *          Expr([Word("bar"), Symbol::ADD, Num(1)]),
 *        ]),
 *      ]),
 *      Expr([Symbol::CONSUME, Word("arr")]),
 *    ]
 *    ```
 *
 * 3. Parsing:
 *
 *    For each statement, transforms the flat list of tokens into an expression
 *    tree.
 *
 *    E.g.,
 *    ```
 *    [
 *      Op(
 *        ASSIGN,
 *        Var("arr"),
 *        Array(
 *          Var("foo"),
 *          Op(
 *            ADD,
 *            Var("bar"),
 *            Num(1),
 *          ),
 *        ),
 *      ),
 *      Op(
 *        CONSUME,
 *        Var("arr"),
 *      ),
 *    ]
 *    ```
 *
 * 4. Serialization:
 *
 *    Converts the expression trees into the corresponding CBOR tree and
 *    serializes that tree to its binary representation.
 */

std::string Compiler::compile(
        poly::string_view source,
        poly::string_view filename) const
{
    const Source src{ source, filename };
    const auto tokens = tokenizer_.tokenize(src);
    const auto groups = grouper_.group(tokens);
    const auto tree   = parser_.parse(groups);
    return serializer_.serialize(tree, src);
}

Compiler::Options::Options() {}

Compiler::Options& Compiler::Options::with_log(std::ostream& os) &
{
    log_os = &os;
    return *this;
}
Compiler::Options&& Compiler::Options::with_log(std::ostream& os) &&
{
    return std::move(with_log(os));
}

Compiler::Options& Compiler::Options::with_verbosity(int v) &
{
    verbosity = v;
    return *this;
}
Compiler::Options&& Compiler::Options::with_verbosity(int v) &&
{
    return std::move(with_verbosity(v));
}

Compiler::Options& Compiler::Options::with_more_verbose() &
{
    verbosity++;
    return *this;
}
Compiler::Options&& Compiler::Options::with_more_verbose() &&
{
    return std::move(with_more_verbose());
}

Compiler::Options& Compiler::Options::with_less_verbose() &
{
    verbosity--;
    return *this;
}
Compiler::Options&& Compiler::Options::with_less_verbose() &&
{
    return std::move(with_less_verbose());
}

Compiler::Options& Compiler::Options::with_debug_info(bool d) &
{
    include_debug_info = d;
    return *this;
}
Compiler::Options&& Compiler::Options::with_debug_info(bool d) &&
{
    return std::move(with_debug_info(d));
}

Compiler::Options& Compiler::Options::with_no_debug_info() &
{
    include_debug_info = false;
    return *this;
}
Compiler::Options&& Compiler::Options::with_no_debug_info() &&
{
    return std::move(with_no_debug_info());
}
} // namespace openzl::sddl
