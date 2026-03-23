// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <stdexcept>

#include "openzl/cpp/poly/StringView.hpp"

#include "tools/sddl2/compiler/Source.h"

namespace openzl::sddl2 {

class CompilerException : public std::runtime_error {
   protected:
    CompilerException(
            const SourceLocation& loc,
            const poly::string_view& error_type,
            const poly::string_view& msg);

   public:
    const SourceLocation& loc() const noexcept;

    const char* what() const noexcept override;

   private:
    const SourceLocation loc_;
    const std::string msg_;
};

class SyntaxError : public CompilerException {
   public:
    SyntaxError(const SourceLocation& loc, const poly::string_view& msg)
            : CompilerException(loc, "syntax error", msg)
    {
    }
};

class ParseError : public CompilerException {
   public:
    ParseError(const SourceLocation& loc, const poly::string_view& msg)
            : CompilerException(loc, "parse error", msg)
    {
    }
};

class CodegenError : public CompilerException {
   public:
    CodegenError(const SourceLocation& loc, const poly::string_view& msg)
            : CompilerException(loc, "code generation error", msg)
    {
    }

    explicit CodegenError(const poly::string_view& msg)
            : CodegenError(SourceLocation::null(), msg)
    {
    }
};

class SemanticError : public CompilerException {
   public:
    SemanticError(const SourceLocation& loc, const poly::string_view& msg)
            : CompilerException(loc, "semantic error", msg)
    {
    }

    explicit SemanticError(const poly::string_view& msg)
            : SemanticError(SourceLocation::null(), msg)
    {
    }
};

class InvariantViolation : public CompilerException {
   public:
    InvariantViolation(const SourceLocation& loc, const poly::string_view& msg)
            : CompilerException(loc, "internal error", msg)
    {
    }

    explicit InvariantViolation(const poly::string_view& msg)
            : InvariantViolation(SourceLocation::null(), msg)
    {
    }
};

/**
 * Shouldn't ever be thrown.
 *
 * Useful though for printing contextual information in the same format as
 * error messages. E.g.,
 *
 * ```
 * log << InfoError(loc, "Originally declared here:").what();
 * ```
 */
class InfoError : public CompilerException {
   public:
    InfoError(const SourceLocation& loc, const poly::string_view& msg)
            : CompilerException(loc, "note", msg)
    {
    }

    explicit InfoError(const poly::string_view& msg)
            : InfoError(SourceLocation::null(), msg)
    {
    }
};

} // namespace openzl::sddl2
