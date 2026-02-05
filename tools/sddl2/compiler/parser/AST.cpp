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

// TODO: message
ASTPoison::ASTPoison(const Token& token, const ASTPtr& paren_ptr)
        : ASTField(token.loc() + maybe_loc(paren_ptr))
{
    // validate_args(paren_ptr);
}

void ASTPoison::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Field: POISON" << std::endl;
}

void ASTPoison::validate_args(const ASTPtr& paren_ptr)
{
    const auto* paren = paren_ptr->as_list();
    if (paren == nullptr) {
        throw InvariantViolation(
                loc(),
                "Field declaration must be given a parenthesized argument list.");
    }
    if (paren->nodes().size() != 0) {
        throw ParseError(loc(), "Poison field declaration takes 0 arguments.");
    }
}

ASTAtom::ASTAtom(const Token& token, const ASTPtr& paren_ptr)
        : ASTField(token.loc() + some(paren_ptr).loc()),
          width_(extract_width_arg(loc(), paren_ptr))
{
}

void ASTAtom::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Field: ATOM:" << std::endl;
    width_->print(os, indent + 2);
}

ASTPtr ASTAtom::extract_width_arg(
        const SourceLocation& loc,
        const ASTPtr& paren_ptr)
{
    const auto* paren = paren_ptr->as_list();
    if (paren == nullptr) {
        throw InvariantViolation(
                loc,
                "Field declaration must be given a parenthesized argument list.");
    }
    const auto& nodes = paren->nodes();
    if (nodes.size() != 1) {
        throw ParseError(
                loc, "Atom field declaration requires exactly 1 argument.");
    }
    return nodes[0];
}

ASTBuiltinField::ASTBuiltinField(const Token& token)
        : ASTField(token.loc()), kw_(token.sym())
{
}

void ASTBuiltinField::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Field: " << sym_to_debug_str(kw_)
       << std::endl;
}

ASTRecord::ASTRecord(const ASTPtr& paren_ptr)
        : ASTField(some(paren_ptr).loc()),
          fields_(extract_fields(loc(), paren_ptr))
{
}

void ASTRecord::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Field: RECORD:" << std::endl;
    for (const auto& field : fields_) {
        field->print(os, indent + 2);
    }
}

ASTVec ASTRecord::extract_fields(
        const SourceLocation& loc,
        const ASTPtr& paren_ptr)
{
    const auto* list = paren_ptr->as_list();
    if (list == nullptr) {
        throw InvariantViolation(
                loc, "Record declaration must be given a list as argument.");
    }
    if (list->list_type() != ListType::CURLY) {
        throw InvariantViolation(
                loc, "Record declaration argument list must be curly-braced.");
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

ASTDest::ASTDest(const Token& token, const ASTPtr& paren_ptr)
        : ASTConverted(token.loc() + maybe_loc(paren_ptr))
{
    // validate_args(paren_ptr);
}

void ASTDest::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Dest" << std::endl;
}

void ASTDest::validate_args(const ASTPtr& paren_ptr)
{
    const auto* paren = paren_ptr->as_list();
    if (paren == nullptr) {
        throw InvariantViolation(
                loc(),
                "Dest declaration must be given a parenthesized argument list.");
    }
    if (paren->nodes().size() != 0) {
        throw ParseError(loc(), "Dest declaration takes 0 arguments.");
    }
}

ASTOp::ASTOp(const Token& token, ASTVec args)
        : ASTConverted(token.loc() + join_locs(args)),
          op_(token.sym()),
          args_(std::move(args))
{
}

void ASTOp::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Op: " << sym_to_debug_str(op_)
       << std::endl;
    for (const auto& arg : args_) {
        arg->print(os, indent + 2);
    }
}

ASTFunc::ASTFunc(ASTVec args, ASTVec body)
        : ASTConverted(join_locs(args) + join_locs(body)),
          args_(std::move(args)),
          body_(std::move(body))
{
}

void ASTFunc::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Func:" << std::endl;
    os << std::string(indent + 2, ' ') << "Args:" << std::endl;
    for (const auto& arg : args_) {
        arg->print(os, indent + 4);
    }
    os << std::string(indent + 2, ' ') << "Body:" << std::endl;
    for (const auto& expr : body_) {
        expr->print(os, indent + 4);
    }
}

ASTTuple::ASTTuple(ASTPtr list)
        : ASTConverted(some(list).loc()), tuple_(extract_exprs(loc(), list))
{
}

void ASTTuple::print(std::ostream& os, size_t indent) const
{
    os << std::string(indent, ' ') << "Tuple:" << std::endl;
    for (const auto& expr : tuple_) {
        expr->print(os, indent + 2);
    }
}

ASTVec ASTTuple::extract_exprs(
        const SourceLocation& loc,
        const ASTPtr& paren_ptr)
{
    const auto* list = paren_ptr->as_list();
    if (list == nullptr) {
        throw InvariantViolation(
                loc, "Tuple declaration must be given a list as argument.");
    }
    if (list->list_type() != ListType::PAREN) {
        throw InvariantViolation(
                loc, "Tuple declaration argument list must be curly-braced.");
    }
    return unwrap_parens(list->nodes());
}
} // namespace openzl::sddl2
