// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl/compiler/AST.h"

#include "tools/sddl/compiler/Exception.h"
#include "tools/sddl/compiler/Syntax.h"
#include "tools/sddl/compiler/Utils.h"

using namespace openzl::sddl::detail;

namespace openzl::sddl {

namespace {

void add_debug_info(
        const ASTNode& node,
        const SerializationOptions& opts,
        const A1C_MapBuilder& map_builder)
{
    if (!opts.include_source_locations) {
        return;
    }

    const auto loc   = node.loc();
    auto* const pair = A1C_MapBuilder_add(map_builder);
    if (pair == nullptr) {
        throw SerializationError(
                loc, "Failed to add debug element to node map.");
    }

    A1C_Item_string_refCStr(&pair->key, "dbg");

    const auto dbg_map_builder =
            A1C_Item_map_builder(&pair->val, 1, opts.arena);
    auto* const loc_pair = A1C_MapBuilder_add(dbg_map_builder);
    if (loc_pair == nullptr) {
        throw SerializationError(
                loc, "Failed to add location element to debug info map.");
    }

    A1C_Item_string_refCStr(&loc_pair->key, "loc");

    auto* const loc_items = A1C_Item_array(&loc_pair->val, 2, opts.arena);
    if (loc_items == nullptr) {
        throw SerializationError(
                loc, "Failed to add location array to debug info map.");
    }

    A1C_Item_int64(&loc_items[0], loc.start());
    A1C_Item_int64(&loc_items[1], loc.size());
}

} // anonymous namespace

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

A1C_Item ASTSym::serialize(const SerializationOptions&) const
{
    throw InvariantViolation(
            loc(),
            "Attempting to serialize AST which contains unconverted symbols!");
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

A1C_Item ASTList::serialize(const SerializationOptions&) const
{
    throw ParseError(
            loc(),
            "Attempting to serialize AST which still contains a list expression which hasn't been consumed or implicitly unwrapped.");
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

A1C_Item ASTNum::serialize(const SerializationOptions& opts) const
{
    auto* const arena = opts.arena;
    A1C_Item map;
    const auto builder = A1C_Item_map_builder(&map, 2, arena);
    auto* const pair   = A1C_MapBuilder_add(builder);
    if (pair == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item map.");
    }
    if (!A1C_Item_string_cstr(&pair->key, "int", arena)) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item string.");
    }
    A1C_Item_int64(&pair->val, val_);

    add_debug_info(*this, opts, builder);
    return map;
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

A1C_Item ASTVar::serialize(const SerializationOptions& opts) const
{
    auto* const arena = opts.arena;
    A1C_Item map;
    const auto builder = A1C_Item_map_builder(&map, 2, arena);
    auto* const pair   = A1C_MapBuilder_add(builder);
    if (pair == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item map.");
    }
    if (!A1C_Item_string_cstr(&pair->key, "var", arena)) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item string.");
    }
    if (!A1C_Item_string_copy(&pair->val, name_.data(), name_.size(), arena)) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item string.");
    }

    add_debug_info(*this, opts, builder);
    return map;
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

A1C_Item ASTPoison::serialize(const SerializationOptions& opts) const
{
    auto* const arena = opts.arena;
    A1C_Item map;
    const auto builder = A1C_Item_map_builder(&map, 2, arena);
    auto* const pair   = A1C_MapBuilder_add(builder);
    if (pair == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item map.");
    }
    const auto tag = sym_to_ser_str(Symbol::POISON);
    A1C_Item_string_ref(&pair->key, tag.data(), tag.size());
    A1C_Item_null(&pair->val);

    add_debug_info(*this, opts, builder);
    return map;
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

A1C_Item ASTAtom::serialize(const SerializationOptions& opts) const
{
    auto* const arena = opts.arena;
    A1C_Item map;
    const auto builder = A1C_Item_map_builder(&map, 2, arena);
    auto* const pair   = A1C_MapBuilder_add(builder);
    if (pair == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item map.");
    }
    const auto tag = sym_to_ser_str(Symbol::ATOM);
    A1C_Item_string_ref(&pair->key, tag.data(), tag.size());
    pair->val = width_->serialize(opts);

    add_debug_info(*this, opts, builder);
    return map;
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

A1C_Item ASTBuiltinField::serialize(const SerializationOptions& opts) const
{
    auto* const arena = opts.arena;
    A1C_Item map;
    const auto builder = A1C_Item_map_builder(&map, 2, arena);
    auto* const pair   = A1C_MapBuilder_add(builder);
    if (pair == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item map.");
    }
    const auto tag = sym_to_ser_str(Symbol::ATOM);
    A1C_Item_string_ref(&pair->key, tag.data(), tag.size());
    const auto kw_name = sym_to_ser_str(kw_);
    A1C_Item_string_ref(&pair->val, kw_name.data(), kw_name.size());

    add_debug_info(*this, opts, builder);
    return map;
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

A1C_Item ASTRecord::serialize(const SerializationOptions& opts) const
{
    auto* const arena = opts.arena;
    A1C_Item map;
    const auto builder = A1C_Item_map_builder(&map, 2, arena);
    auto* const pair   = A1C_MapBuilder_add(builder);
    if (pair == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item map.");
    }
    const auto tag = sym_to_ser_str(Symbol::RECORD);
    A1C_Item_string_ref(&pair->key, tag.data(), tag.size());
    auto* const items = A1C_Item_array(&pair->val, fields_.size(), arena);
    if (items == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item array.");
    }
    for (size_t i = 0; i < fields_.size(); i++) {
        items[i] = fields_[i]->serialize(opts);
    }

    add_debug_info(*this, opts, builder);
    return map;
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

A1C_Item ASTArray::serialize(const SerializationOptions& opts) const
{
    auto* const arena = opts.arena;
    A1C_Item map;
    const auto builder = A1C_Item_map_builder(&map, 2, arena);
    auto* const pair   = A1C_MapBuilder_add(builder);
    if (pair == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item map.");
    }
    const auto tag = sym_to_ser_str(Symbol::ARRAY);
    A1C_Item_string_ref(&pair->key, tag.data(), tag.size());
    auto* const items = A1C_Item_array(&pair->val, 2, arena);
    if (items == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item array.");
    }
    items[0] = field_->serialize(opts);
    items[1] = len_->serialize(opts);

    add_debug_info(*this, opts, builder);
    return map;
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

A1C_Item ASTDest::serialize(const SerializationOptions& opts) const
{
    auto* const arena = opts.arena;
    A1C_Item map;
    const auto builder = A1C_Item_map_builder(&map, 2, arena);
    auto* const pair   = A1C_MapBuilder_add(builder);
    if (pair == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item map.");
    }
    const auto tag = sym_to_ser_str(Symbol::DEST);
    A1C_Item_string_ref(&pair->key, tag.data(), tag.size());
    A1C_Item_null(&pair->val);

    add_debug_info(*this, opts, builder);
    return map;
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

A1C_Item ASTOp::serialize(const SerializationOptions& opts) const
{
    auto* const arena = opts.arena;
    A1C_Item map;
    const auto builder = A1C_Item_map_builder(&map, 2, arena);
    auto* const pair   = A1C_MapBuilder_add(builder);
    if (pair == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item map.");
    }
    const auto op_name = sym_to_ser_str(op_);
    A1C_Item_string_ref(&pair->key, op_name.data(), op_name.size());
    if (args_.size() == 0) {
        A1C_Item_null(&pair->val);
    } else if (args_.size() == 1) {
        pair->val = args_[0]->serialize(opts);
    } else {
        auto* const items = A1C_Item_array(&pair->val, args_.size(), arena);
        if (items == nullptr) {
            throw SerializationError(
                    loc(), "Failed to allocate A1C_Item array.");
        }
        for (size_t i = 0; i < args_.size(); i++) {
            items[i] = args_[i]->serialize(opts);
        }
    }

    add_debug_info(*this, opts, builder);
    return map;
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

A1C_Item ASTFunc::serialize(const SerializationOptions& opts) const
{
    auto* const arena = opts.arena;
    A1C_Item map;
    const auto builder = A1C_Item_map_builder(&map, 2, arena);
    auto* const pair   = A1C_MapBuilder_add(builder);
    if (pair == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item map.");
    }
    A1C_Item_string_refCStr(&pair->key, "func");

    auto* const val_items = A1C_Item_array(&pair->val, 2, arena);
    if (val_items == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item array.");
    }

    auto* const arg_items  = A1C_Item_array(&val_items[0], args_.size(), arena);
    auto* const body_items = A1C_Item_array(&val_items[1], body_.size(), arena);

    if (args_.size() != 0 && arg_items == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item array.");
    }
    if (body_.size() != 0 && body_items == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item array.");
    }

    for (size_t i = 0; i < args_.size(); i++) {
        arg_items[i] = some(args_[i]).serialize(opts);
    }
    for (size_t i = 0; i < body_.size(); i++) {
        body_items[i] = some(body_[i]).serialize(opts);
    }

    add_debug_info(*this, opts, builder);
    return map;
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

A1C_Item ASTTuple::serialize(const SerializationOptions& opts) const
{
    auto* const arena = opts.arena;
    A1C_Item map;
    const auto builder = A1C_Item_map_builder(&map, 2, arena);
    auto* const pair   = A1C_MapBuilder_add(builder);
    if (pair == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item map.");
    }
    A1C_Item_string_refCStr(&pair->key, "tuple");

    auto* const val_items = A1C_Item_array(&pair->val, tuple_.size(), arena);
    if (val_items == nullptr) {
        throw SerializationError(loc(), "Failed to allocate A1C_Item array.");
    }

    for (size_t i = 0; i < tuple_.size(); i++) {
        val_items[i] = some(tuple_[i]).serialize(opts);
    }

    add_debug_info(*this, opts, builder);
    return map;
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
} // namespace openzl::sddl
