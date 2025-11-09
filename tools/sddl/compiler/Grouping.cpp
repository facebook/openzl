// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl/compiler/Grouping.h"

#include "tools/sddl/compiler/Exception.h"
#include "tools/sddl/compiler/Utils.h"

using namespace openzl::sddl::detail;

namespace openzl::sddl {

GroupingNode::GroupingNode(SourceLocation loc) : loc_(std::move(loc)) {}

const SourceLocation& GroupingNode::loc() const
{
    return loc_;
}

const GroupingToken* GroupingNode::as_token() const
{
    return nullptr;
}

const GroupingList* GroupingNode::as_list() const
{
    return nullptr;
}

const GroupingExpr* GroupingNode::as_expr() const
{
    return nullptr;
}

bool GroupingNode::operator==(const Symbol& sym) const
{
    const auto* tok = as_token();
    if (tok == nullptr) {
        return false;
    }
    return **tok == sym;
}

bool GroupingNode::operator!=(const Symbol& sym) const
{
    return !(*this == sym);
}

std::ostream& operator<<(std::ostream& os, const GroupingNode& node)
{
    node.print(os);
    return os;
}

GroupingToken::GroupingToken(Token tok)
        : GroupingNode(tok.loc()), tok_(std::move(tok))
{
}

const GroupingToken* GroupingToken::as_token() const
{
    return this;
}

const Token& GroupingToken::operator*() const
{
    return tok_;
}

void GroupingToken::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Token: " << std::endl;
    os << tok_.str(indent + 2);
}

GroupingList::GroupingList(
        ListType type,
        GroupingPtr open,
        GroupingPtr close,
        GroupingVec nodes)
        : GroupingNode(join_locs(nodes) + some(open).loc() + some(close).loc()),
          type_(type),
          open_(std::move(open)),
          close_(std::move(close)),
          nodes_(std::move(nodes))
{
}

const GroupingList* GroupingList::as_list() const
{
    return this;
}

ListType GroupingList::type() const
{
    return type_;
}

const GroupingVec& GroupingList::nodes() const
{
    return nodes_;
}

const GroupingPtr& GroupingList::open() const
{
    return open_;
}

const GroupingPtr& GroupingList::close() const
{
    return close_;
}

void GroupingList::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "List:" << std::endl;
    os << std::string(indent + 2, ' ')
       << "Type: " << list_type_to_debug_str(type()) << std::endl;
    for (const auto& ptr : nodes()) {
        ptr->print(os, indent + 2);
    }
}

GroupingExpr::GroupingExpr(GroupingVec nodes, GroupingPtr terminator)
        : GroupingNode(join_locs(nodes) + some(terminator).loc()),
          nodes_(std::move(nodes)),
          terminator_(std::move(terminator))
{
}

const GroupingExpr* GroupingExpr::as_expr() const
{
    return this;
}

const GroupingVec& GroupingExpr::nodes() const
{
    return nodes_;
}

const GroupingPtr& GroupingExpr::terminator() const
{
    return terminator_;
}

void GroupingExpr::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Expression: " << std::endl;
    for (const auto& ptr : nodes()) {
        ptr->print(os, indent + 2);
    }
}

} // namespace openzl::sddl
