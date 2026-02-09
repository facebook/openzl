// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <iostream>

#include "tools/sddl2/compiler/Exception.h"
#include "tools/sddl2/compiler/Syntax.h"
#include "tools/sddl2/compiler/Utils.h"
#include "tools/sddl2/compiler/parser/AST.h"

using namespace openzl::sddl2::detail;

namespace openzl::sddl2 {
ASTNode::ASTNode(SourceLocation loc) : loc_(std::move(loc)) {}

const SourceLocation& ASTNode::loc() const
{
    return loc_;
}

const ASTSym* ASTNode::as_sym() const
{
    return nullptr;
}

const ASTList* ASTNode::as_list() const
{
    return nullptr;
}

bool ASTNode::operator==(const Symbol& sym) const
{
    const auto* tok = as_sym();
    if (tok == nullptr) {
        return false;
    }
    return **tok == sym;
}

bool ASTNode::operator!=(const Symbol& sym) const
{
    return !(*this == sym);
}

ASTSym::ASTSym(const Token& token)
        : ASTUnconverted(token.loc()), sym_(token.sym())
{
}

const ASTSym* ASTSym::as_sym() const
{
    return this;
}

const Symbol& ASTSym::operator*() const
{
    return sym_;
}

void ASTSym::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Symbol: " << sym_to_debug_str(sym_)
       << std::endl;
}

ASTList::ASTList(
        ListType type,
        const ASTPtr& open,
        const ASTPtr& close,
        ASTVec nodes)
        : ASTUnconverted(
                  join_locs(nodes) + some(open).loc() + some(close).loc()),
          type_(type),
          nodes_(unwrap_parens(std::move(nodes)))
{
}

const ASTList* ASTList::as_list() const
{
    return this;
}

ListType ASTList::list_type() const
{
    return type_;
}

const ASTVec& ASTList::nodes() const
{
    return nodes_;
}

void ASTList::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "List:" << std::endl;
    os << std::string(indent + 2, ' ')
       << "Type: " << list_type_to_debug_str(list_type()) << std::endl;
    for (const auto& ptr : nodes_) {
        ptr->print(os, indent + 2);
    }
}

ASTPtr unwrap_parens(ASTPtr arg_ptr)
{
    while (true) {
        const auto& arg  = some(arg_ptr);
        const auto* list = arg.as_list();
        if (list == nullptr) {
            return arg_ptr;
        }
        if (list->list_type() != ListType::PAREN) {
            return arg_ptr;
        }
        const auto inner_nodes = list->nodes();
        if (inner_nodes.size() != 1) {
            return arg_ptr;
        }
        arg_ptr = inner_nodes[0];
    }
}

ASTVec unwrap_parens(ASTVec nodes)
{
    for (auto& node : nodes) {
        node = unwrap_parens(std::move(node));
    }
    return nodes;
}

const ASTVec& unwrap_square(const ASTPtr& arg_ptr)
{
    const auto& arg  = some(arg_ptr);
    const auto* list = arg.as_list();
    if (list == nullptr) {
        throw InvariantViolation(arg.loc(), "Expected square-braced list.");
    }
    if (list->list_type() != ListType::SQUARE) {
        throw InvariantViolation(arg.loc(), "Expected square-braced list.");
    }
    return list->nodes();
}

const ASTVec& unwrap_curly(const ASTPtr& arg_ptr)
{
    const auto& arg  = some(arg_ptr);
    const auto* list = arg.as_list();
    if (list == nullptr) {
        throw InvariantViolation(arg.loc(), "Expected curly-braced list.");
    }
    if (list->list_type() != ListType::CURLY) {
        throw InvariantViolation(arg.loc(), "Expected curly-braced list.");
    }
    return list->nodes();
}

ASTNum::ASTNum(const Token& token)
        : ASTConverted(token.loc()), val_(token.num())
{
}

void ASTNum::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Num: " << val_ << std::endl;
}

int64_t ASTNum::val() const
{
    return val_;
}

ASTVar::ASTVar(const Token& token)
        : ASTConverted(token.loc()), name_(token.word())
{
}

void ASTVar::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Var: " << name_ << std::endl;
}

ASTBuiltinField::ASTBuiltinField(const SourceLocation& loc, const Op& op)
        : ASTField(loc), kw_(op)
{
}

void ASTBuiltinField::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Field: " << op_to_debug_str(kw_)
       << std::endl;
}

ASTBytes::ASTBytes(const ASTPtr& len)
        : ASTField(some(len).loc()), len_(extract_len(loc(), len))
{
}

void ASTBytes::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Field: BYTES:" << std::endl;
    os << std::string(indent + 2, ' ') << "Len: " << std::endl;
    len_->print(os, indent + 4);
}

ASTPtr ASTBytes::extract_len(const SourceLocation& loc, const ASTPtr& paren_ptr)
{
    const auto* list = paren_ptr->as_list();
    if (list == nullptr) {
        throw InvariantViolation(
                paren_ptr->loc(),
                "Bytes declaration must be given a list of params.");
    }
    if (list->list_type() != ListType::PAREN) {
        throw InvariantViolation(
                paren_ptr->loc(),
                "Bytes declaration params list must be parens.");
    }
    if (list->nodes().size() != 1) {
        throw InvariantViolation(
                paren_ptr->loc(),
                "Bytes declaration must be given a single param.");
    }
    return list->nodes()[0];
}

ASTRecord::ASTRecord(const ASTPtr& params, const ASTPtr& fields)
        : ASTField(params->loc() + fields->loc()),
          params_(extract_params(loc(), params)),
          fields_(extract_fields(loc(), fields))

{
}

void ASTRecord::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Field: RECORD:" << std::endl;
    os << std::string(indent + 2, ' ') << "Captures: " << std::endl;
    for (const auto& capture : params_) {
        capture->print(os, indent + 4);
    }
    os << std::string(indent + 2, ' ') << "Fields: " << std::endl;
    for (const auto& field : fields_) {
        field->print(os, indent + 4);
    }
}

ASTVec ASTRecord::extract_fields(
        const SourceLocation& loc,
        const ASTPtr& paren_ptr)
{
    const auto* list = paren_ptr->as_list();
    if (list == nullptr) {
        throw InvariantViolation(
                loc, "Record declaration must be given a list of fields.");
    }
    if (list->list_type() != ListType::CURLY) {
        throw InvariantViolation(
                loc, "Record declaration fields list must be curly-braced.");
    }
    return unwrap_parens(list->nodes());
}

ASTVec ASTRecord::extract_params(
        const SourceLocation& loc,
        const ASTPtr& paren_ptr)
{
    const auto* list = paren_ptr->as_list();
    if (list == nullptr) {
        throw InvariantViolation(
                loc, "Record declaration must be given a list of params.");
    }
    if (list->list_type() != ListType::PAREN) {
        throw InvariantViolation(
                loc, "Record declaration params list must be parens.");
    }
    return unwrap_parens(list->nodes());
}

ASTArray::ASTArray(const ASTPtr& field, const ASTPtr& len)
        : ASTField(some(field).loc() + some(len).loc()),
          field_(field),
          len_(len)
{
}

void ASTArray::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Field: ARRAY:" << std::endl;
    field_->print(os, indent + 2);
    len_->print(os, indent + 2);
}

ASTOp::ASTOp(const SourceLocation& loc, const Op& op, ASTVec args)
        : ASTConverted(loc + join_locs(args)), op_(op), args_(std::move(args))
{
}

void ASTOp::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Op: " << op_to_debug_str(op_)
       << std::endl;
    for (const auto& arg : args_) {
        arg->print(os, indent + 2);
    }
}
} // namespace openzl::sddl2
