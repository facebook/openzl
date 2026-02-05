// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl2/compiler/Compiler.h"

#include "tools/sddl2/compiler/Source.h"
#include "tools/sddl2/compiler/grouper/Grouper.h"
#include "tools/sddl2/compiler/parser/Parser.h"
#include "tools/sddl2/compiler/tokenizer/Tokenizer.h"

namespace openzl::sddl2 {

Compiler::Compiler(Options options)
        : options_(std::move(options)),
          logger_(*options_.log_os, options_.verbosity),
          tokenizer_(logger_),
          grouper_(logger_),
          parser_(logger_)
{
}

/**
 * The compiler for SDDL is comprised of four passes:
 *
 * ```
 *   Record Entry() = {
 *       id: Int32LE,
 *   }
 *   : Entry
 * ```
 *
 * 1. Tokenization:
 *
 *    Converts the contiguous string of source code into a flat list of tokens.
 *    Strips whitespace and comments.
 *
 *    The previous source code would be tokenized as:
 *    ```
 *    [
 *      Symbol::RECORD, Word("Entry"), Symbol::PAREN_OPEN, Symbol::PAREN_CLOSE,
 *      Symbol: ASSIGN, Symbol::CURLY_OPEN, Symbol::NL, Word("id"),
 *      Symbol: ASSUME, Symbol: I32LE, Symbol::COMMA, Symbol::NL,
 *      Symbol::CURLY_CLOSE, Symbol::NL, Symbol::ASSUME,
 *      Word("Entry"), Symbol::NL
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
 *        Symbol::RECORD, Word("Entry"), List(PAREN, []), Symbol::ASSIGN,
 *        List(CURLY, [Expr([Word("id"), Symbol::ASSUME, Symbol::I32LE])]),
 *      ]),
 *      Expr([
 *        Symbol::ASSUME, Word("Entry"),
 *      ]),
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
 *        Var("Entry"),
 *        Record(
 *          [],
 *          [
 *            Op(
 *              ASSIGN,
 *              Var("id"),
 *              Op(
 *                CONSUME,
 *                Field("I32LE"),
 *              ),
 *            ),
 *          ],
 *        ),
 *      ),
 *      Op(
 *        CONSUME,
 *        Var("Entry"),
 *      ),
 *    ]
 *    ```
 *
 * 4. Serialization: (TODO)
 */

std::string Compiler::compile(
        poly::string_view source,
        poly::string_view filename) const
{
    const Source src{ source, filename };
    const auto tokens = tokenizer_.tokenize(src);
    const auto groups = grouper_.group(tokens);
    const auto tree   = parser_.parse(groups);
    // TODO: serialize the ast
    return "";
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
} // namespace openzl::sddl2
