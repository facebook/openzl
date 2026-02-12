// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <vector>

#include "openzl/shared/a1cbor.h"

#include "tools/sddl2/compiler/Source.h"
#include "tools/sddl2/compiler/parser/Ops.h"
#include "tools/sddl2/compiler/tokenizer/Token.h"

namespace openzl::sddl2 {

struct SerializationOptions {
    A1C_Arena* arena;
    bool include_source_locations;
};

// Forward declarations of subtypes
class ASTSym;
class ASTList;

/**
 * Abstract base class for an AST node.
 */
class ASTNode {
   protected:
    explicit ASTNode(SourceLocation loc);

   public:
    virtual ~ASTNode() = default;

    virtual const ASTSym* as_sym() const;

    virtual const ASTList* as_list() const;

    bool operator==(const Symbol& symbol) const;
    bool operator!=(const Symbol& symbol) const;

    virtual void print(std::ostream& os, size_t indent = 0) const = 0;

    const SourceLocation& loc() const;

   private:
    const SourceLocation loc_;
};

using ASTPtr = std::shared_ptr<const ASTNode>;
using ASTVec = std::vector<ASTPtr>;

ASTPtr unwrap_parens(ASTPtr arg);

ASTVec unwrap_parens(ASTVec args);

const ASTVec& unwrap_square(const ASTPtr& arg);

const ASTVec& unwrap_curly(const ASTPtr& arg);

/**
 * Base class for temporary nodes that cannot appear in the final AST, and that
 * therefore still need to be parsed/converted.
 */
class ASTUnconverted : public ASTNode {
   public:
    using ASTNode::ASTNode;
};

/**
 * Base class for nodes that can appear in the final AST.
 */
class ASTConverted : public ASTNode {
   public:
    using ASTNode::ASTNode;
};

/**
 * Temporary representation of an unparsed token (i.e., corresponds to a
 * GroupingToken). Parsing should transform all ASTSyms into ASTOps.
 */
class ASTSym : public ASTUnconverted {
   public:
    explicit ASTSym(const Token& token);

    const ASTSym* as_sym() const override;

    const Symbol& operator*() const;

    void print(std::ostream& os, size_t indent) const override;

   private:
    const Symbol sym_;
};

/**
 * Temporary representation of an unparsed list (i.e., corresponds to a
 * GroupingList). Parsing should unwrap all lists, either implicitly, when they
 * are parenthesized lists with one element, or explicitly as part of joining
 * the list with an op that consumes a list argument.
 */
class ASTList : public ASTUnconverted {
   public:
    explicit ASTList(
            ListType type,
            const ASTPtr& open,
            const ASTPtr& close,
            ASTVec nodes);

    const ASTList* as_list() const override;

    ListType list_type() const;

    const ASTVec& nodes() const;

    void print(std::ostream& os, size_t indent) const override;

   private:
    const ListType type_;
    const ASTVec nodes_;
};

class ASTNum : public ASTConverted {
   public:
    explicit ASTNum(const Token& token);

    void print(std::ostream& os, size_t indent) const override;

    int64_t val() const;

   private:
    const int64_t val_;
};

class ASTVar : public ASTConverted {
   public:
    explicit ASTVar(const Token& token);

    void print(std::ostream& os, size_t indent) const override;

   private:
    const std::string name_;
};

class ASTField : public ASTConverted {
   public:
    using ASTConverted::ASTConverted;
};

class ASTBuiltinField : public ASTField {
   public:
    explicit ASTBuiltinField(const SourceLocation& loc, const Symbol& op);

    void print(std::ostream& os, size_t indent) const override;

   private:
    const Symbol kw_;
};

class ASTBytes : public ASTField {
   public:
    explicit ASTBytes(const SourceLocation& loc, const ASTPtr& len);

    void print(std::ostream& os, size_t indent) const override;

   private:
    static ASTPtr extract_len(const ASTPtr& paren_ptr);

    const ASTPtr len_;
};

class ASTRecord : public ASTField {
   public:
    explicit ASTRecord(const ASTPtr& params, const ASTPtr& fields);

    void print(std::ostream& os, size_t indent) const override;

   private:
    static ASTVec extract_fields(
            const SourceLocation& loc,
            const ASTPtr& paren_ptr);

    static ASTVec extract_params(
            const SourceLocation& loc,
            const ASTPtr& paren_ptr);

    const ASTVec params_;
    const ASTVec fields_;
};

class ASTArray : public ASTField {
   public:
    explicit ASTArray(const ASTPtr& field, const ASTPtr& len);

    void print(std::ostream& os, size_t indent) const override;

   private:
    const ASTPtr field_;
    const ASTPtr len_;
};

class ASTOp : public ASTConverted {
   public:
    explicit ASTOp(const SourceLocation& loc, const Op& op, ASTVec args);

    void print(std::ostream& os, size_t indent) const override;

   private:
    const Op op_;
    const ASTVec args_;
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
    ASTPtr op(Op op, Args... args) const
    {
        return std::make_shared<ASTOp>(
                SourceLocation::null(), op, vec(std::move(args)...));
    }

    // Ops
    ASTPtr expect(ASTPtr arg) const
    {
        return op(Op::EXPECT, std::move(arg));
    }

    ASTPtr consume(ASTPtr arg) const
    {
        return op(Op::CONSUME, std::move(arg));
    }

    ASTPtr size_of(ASTPtr arg) const
    {
        return op(Op::SIZEOF, std::move(arg));
    }

    ASTPtr assign(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Op::ASSIGN, std::move(lhs), std::move(rhs));
    }

    ASTPtr eq(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Op::EQ, std::move(lhs), std::move(rhs));
    }

    ASTPtr ne(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Op::NE, std::move(lhs), std::move(rhs));
    }

    ASTPtr add(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Op::ADD, std::move(lhs), std::move(rhs));
    }

    ASTPtr sub(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Op::SUB, std::move(lhs), std::move(rhs));
    }

    ASTPtr mul(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Op::MUL, std::move(lhs), std::move(rhs));
    }

    ASTPtr div(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Op::DIV, std::move(lhs), std::move(rhs));
    }

    ASTPtr mod(ASTPtr lhs, ASTPtr rhs) const
    {
        return op(Op::MOD, std::move(lhs), std::move(rhs));
    }

    // Other types of things

    ASTPtr num(int64_t val) const
    {
        return std::make_shared<ASTNum>(Token{ loc_, val });
    }

    ASTPtr builtin_field(Symbol sym) const
    {
        return std::make_shared<ASTBuiltinField>(loc_, sym);
    }

    ASTPtr array(ASTPtr field, ASTPtr len) const
    {
        return std::make_shared<ASTArray>(std::move(field), std::move(len));
    }

    ASTPtr bytes(ASTPtr len) const
    {
        return std::make_shared<ASTBytes>(loc_, paren_list({ std::move(len) }));
    }

    ASTPtr record(ASTVec params, ASTVec fields) const
    {
        return std::make_shared<ASTRecord>(
                paren_list(std::move(params)), curly_list(std::move(fields)));
    }

    ASTPtr var(poly::string_view name) const
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

   private:
    const SourceLocation loc_;
};

} // namespace openzl::sddl2
