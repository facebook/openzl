// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <vector>

#include "openzl/shared/a1cbor.h"

#include "tools/sddl/compiler/Source.h"
#include "tools/sddl/compiler/Token.h"

namespace openzl::sddl {

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

    virtual void print(std::ostream& os, size_t indent = 0) const = 0;

    virtual A1C_Item serialize(const SerializationOptions& opts) const = 0;

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

    A1C_Item serialize(const SerializationOptions& opts) const override;

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

    A1C_Item serialize(const SerializationOptions& opts) const override;

   private:
    const ListType type_;
    const ASTVec nodes_;
};

class ASTNum : public ASTConverted {
   public:
    explicit ASTNum(const Token& token);

    void print(std::ostream& os, size_t indent) const override;

    A1C_Item serialize(const SerializationOptions& opts) const override;

    int64_t val() const;

   private:
    const int64_t val_;
};

class ASTVar : public ASTConverted {
   public:
    explicit ASTVar(const Token& token);

    void print(std::ostream& os, size_t indent) const override;

    A1C_Item serialize(const SerializationOptions& opts) const override;

   private:
    const std::string name_;
};

class ASTField : public ASTConverted {
   public:
    using ASTConverted::ASTConverted;
};

class ASTPoison : public ASTField {
   public:
    explicit ASTPoison(
            const Token& token,
            const ASTPtr& paren_ptr = { nullptr });

    void print(std::ostream& os, size_t indent) const override;

    A1C_Item serialize(const SerializationOptions& opts) const override;

   private:
    void validate_args(const ASTPtr& paren_ptr);
};

class ASTAtom : public ASTField {
   public:
    explicit ASTAtom(const Token& token, const ASTPtr& paren_ptr);

    void print(std::ostream& os, size_t indent) const override;

    A1C_Item serialize(const SerializationOptions& opts) const override;

   private:
    static ASTPtr extract_width_arg(
            const SourceLocation& loc,
            const ASTPtr& paren_ptr);

    const ASTPtr width_;
};

class ASTBuiltinField : public ASTField {
   public:
    explicit ASTBuiltinField(const Token& token);

    void print(std::ostream& os, size_t indent) const override;

    A1C_Item serialize(const SerializationOptions& opts) const override;

   private:
    const Symbol kw_;
};

class ASTRecord : public ASTField {
   public:
    explicit ASTRecord(const ASTPtr& paren_ptr);

    void print(std::ostream& os, size_t indent) const override;

    A1C_Item serialize(const SerializationOptions& opts) const override;

   private:
    static ASTVec extract_fields(
            const SourceLocation& loc,
            const ASTPtr& paren_ptr);

    const ASTVec fields_;
};

class ASTArray : public ASTField {
   public:
    explicit ASTArray(const ASTPtr& field, const ASTPtr& len);

    void print(std::ostream& os, size_t indent) const override;

    A1C_Item serialize(const SerializationOptions& opts) const override;

   private:
    const ASTPtr field_;
    const ASTPtr len_;
};

class ASTDest : public ASTConverted {
   public:
    explicit ASTDest(const Token& token, const ASTPtr& paren_ptr);

    void print(std::ostream& os, size_t indent) const override;

    A1C_Item serialize(const SerializationOptions& opts) const override;

   private:
    void validate_args(const ASTPtr& paren_ptr);
};

class ASTOp : public ASTConverted {
   public:
    explicit ASTOp(const Token& token, ASTVec args);

    void print(std::ostream& os, size_t indent) const override;

    A1C_Item serialize(const SerializationOptions& opts) const override;

   private:
    const Symbol op_;
    const ASTVec args_;
};

class ASTFunc : public ASTConverted {
   public:
    explicit ASTFunc(ASTVec args, ASTVec body);

    void print(std::ostream& os, size_t indent) const override;

    A1C_Item serialize(const SerializationOptions& opts) const override;

   private:
    const ASTVec args_;
    const ASTVec body_;
};

class ASTTuple : public ASTConverted {
   public:
    explicit ASTTuple(ASTPtr list);

    void print(std::ostream& os, size_t indent) const override;

    A1C_Item serialize(const SerializationOptions& opts) const override;

   private:
    static ASTVec extract_exprs(
            const SourceLocation& loc,
            const ASTPtr& paren_ptr);

    const ASTVec tuple_;
};

} // namespace openzl::sddl
