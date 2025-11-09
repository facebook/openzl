// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <ostream>
#include <vector>

#include "openzl/shared/a1cbor.h"

#include "tools/sddl/compiler/Source.h"
#include "tools/sddl/compiler/Syntax.h"
#include "tools/sddl/compiler/Token.h"

namespace openzl::sddl {

// Forward declarations of subtypes
class GroupingToken;
class GroupingList;
class GroupingExpr;

class GroupingNode {
   protected:
    explicit GroupingNode(SourceLocation loc);

   public:
    virtual ~GroupingNode() = default;

    virtual const GroupingToken* as_token() const;

    virtual const GroupingList* as_list() const;

    virtual const GroupingExpr* as_expr() const;

    bool operator==(const Symbol& symbol) const;

    bool operator!=(const Symbol& symbol) const;

    virtual void print(std::ostream& os, size_t indent = 0) const = 0;

    const SourceLocation& loc() const;

   protected:
    const SourceLocation loc_;
};

std::ostream& operator<<(std::ostream& os, const GroupingNode& node);

using GroupingPtr = std::shared_ptr<const GroupingNode>;
using GroupingVec = std::vector<GroupingPtr>;

class GroupingToken : public GroupingNode {
   public:
    explicit GroupingToken(Token tok);

    const GroupingToken* as_token() const override;

    const Token& operator*() const;

    void print(std::ostream& os, size_t indent) const override;

   private:
    const Token tok_;
};

class GroupingList : public GroupingNode {
   public:
    explicit GroupingList(
            ListType type,
            GroupingPtr open,
            GroupingPtr close,
            GroupingVec nodes);

    const GroupingList* as_list() const override;

    ListType type() const;

    const GroupingVec& nodes() const;

    const GroupingPtr& open() const;

    const GroupingPtr& close() const;

    void print(std::ostream& os, size_t indent) const override;

   private:
    const ListType type_;
    const GroupingPtr open_;
    const GroupingPtr close_;
    const GroupingVec nodes_;
};

class GroupingExpr : public GroupingNode {
   public:
    explicit GroupingExpr(GroupingVec nodes, GroupingPtr terminator);

    const GroupingExpr* as_expr() const override;

    const GroupingVec& nodes() const;

    const GroupingPtr& terminator() const;

    void print(std::ostream& os, size_t indent) const override;

   private:
    const GroupingVec nodes_;
    const GroupingPtr terminator_;
};

} // namespace openzl::sddl
