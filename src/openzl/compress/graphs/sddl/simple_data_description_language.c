// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/compress/graphs/sddl/simple_data_description_language.h"

#include <stddef.h>
#include <stdint.h>

#include "openzl/codecs/zl_sddl.h"
#include "openzl/zl_reflection.h"

#include "openzl/shared/a1cbor.h"
#include "openzl/shared/mem.h"

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/limits.h"
#include "openzl/common/map.h"
#include "openzl/common/operation_context.h"
#include "openzl/common/vector.h"

#include "openzl/compress/graphs/sddl/simple_data_description_language_source_code.h"

////////////////////////////////////////
// Misc Utilities
////////////////////////////////////////

static ZL_RESULT_OF(StringView)
        mk_sv_n(Arena* const arena, const char* str, const size_t len)
{
    ZL_RESULT_DECLARE_SCOPE(StringView, NULL);
    char* buf = ALLOC_Arena_malloc(arena, len + 1);
    ZL_ERR_IF_NULL(buf, allocation);
    memcpy(buf, str, len);
    buf[len] = '\0';
    return ZL_WRAP_VALUE(StringView_init(buf, len));
}

typedef struct {
    size_t refs;
} ZL_SDDL_RefCount;

/// Creates a new refcount with count == 1.
static ZL_SDDL_RefCount ZL_SDDL_RefCount_create(void)
{
    return (ZL_SDDL_RefCount){ .refs = 1 };
}

static void ZL_SDDL_RefCount_destroy(ZL_SDDL_RefCount* refs)
{
    ZL_ASSERT_EQ(refs->refs, 0);
}

static void ZL_SDDL_RefCount_incref(ZL_SDDL_RefCount* const refs)
{
    refs->refs++;
}

static bool ZL_SDDL_RefCount_decref(
        ZL_SDDL_RefCount* const refs,
        void (*free_func)(void* obj, void* ctx),
        void* obj,
        void* ctx)
{
    ZL_ASSERT_NE(refs->refs, 0);
    if (refs->refs == 0) {
        return false;
    }
    refs->refs--;
    if (refs->refs == 0) {
        free_func(obj, ctx);
        return true;
    }
    return false;
}

static size_t ZL_SDDL_RefCount_count(const ZL_SDDL_RefCount* const refs)
{
    return refs->refs;
}

/*****************
 * Program Types *
 *****************/

// Forward Declaration
typedef struct ZL_SDDL_Expr_s ZL_SDDL_Expr;
typedef struct ZL_SDDL_DynExprSet_s ZL_SDDL_DynExprSet;
typedef struct ZL_SDDL_CachedInstructions_s ZL_SDDL_CachedInstructions;

ZL_DECLARE_PREDEF_MAP_TYPE(ZL_SDDL_VarMap, StringView, ZL_SDDL_Expr*);

typedef int64_t ZL_SDDL_IntT;

typedef struct {
    ZL_SDDL_IntT val;
} ZL_SDDL_Number;

typedef struct {
    uint32_t dest;
} ZL_SDDL_Dest;

typedef struct {
    StringView msg;
} ZL_SDDL_Field_Poison;

typedef struct {
    const ZL_SDDL_Expr* width_expr;

    size_t width;

    ZL_Type type;

    // For numeric types
    bool is_integer;
    bool is_signed;
    bool is_big_endian;

    ZL_SDDL_Dest dest;
} ZL_SDDL_Field_Atom;

typedef struct {
    const ZL_SDDL_Expr** exprs;
    size_t num_exprs;
    ZL_SDDL_DynExprSet* dyn;
} ZL_SDDL_Field_Record;

typedef struct {
    const ZL_SDDL_Expr* expr;
    const ZL_SDDL_Expr* len;
    ZL_SDDL_DynExprSet* dyn;
} ZL_SDDL_Field_Array;

typedef enum {
    ZL_SDDL_FieldType_poison,
    ZL_SDDL_FieldType_atom,
    ZL_SDDL_FieldType_record,
    ZL_SDDL_FieldType_array,
} ZL_SDDL_FieldType;

typedef struct {
    ZL_SDDL_FieldType type;
    union {
        ZL_SDDL_Field_Poison poison;
        ZL_SDDL_Field_Atom atom;
        ZL_SDDL_Field_Record record;
        ZL_SDDL_Field_Array array;
    };
} ZL_SDDL_Field;

typedef struct {
    StringView name;
} ZL_SDDL_Var;

typedef struct {
    ZL_SDDL_RefCount refs;

    ZL_SDDL_VarMap vars;
} ZL_SDDL_Scope;

typedef struct {
    const ZL_SDDL_Expr* exprs;
    size_t num_exprs;
} ZL_SDDL_Tuple;

typedef struct {
    const ZL_SDDL_Expr* exprs;
    size_t num_exprs;

    const ZL_SDDL_Var* unbound_args;
    size_t num_unbound_args;

    ZL_SDDL_Scope* scope;
} ZL_SDDL_Func;

typedef enum {
    ZL_SDDL_OpCode_die,
    ZL_SDDL_OpCode_expect,
    ZL_SDDL_OpCode_log,

    ZL_SDDL_OpCode_consume,
    ZL_SDDL_OpCode_sizeof,
    ZL_SDDL_OpCode_send,
    ZL_SDDL_OpCode_assign,
    ZL_SDDL_OpCode_member,

    ZL_SDDL_OpCode_bind,

    // Unary arithmetic operations
    ZL_SDDL_OpCode_neg,

    // Binary arithmetic operations
    ZL_SDDL_OpCode_eq,
    ZL_SDDL_OpCode_ne,
    ZL_SDDL_OpCode_gt,
    ZL_SDDL_OpCode_ge,
    ZL_SDDL_OpCode_lt,
    ZL_SDDL_OpCode_le,
    ZL_SDDL_OpCode_add,
    ZL_SDDL_OpCode_sub,
    ZL_SDDL_OpCode_mul,
    ZL_SDDL_OpCode_div,
    ZL_SDDL_OpCode_mod,

    // Bitwise operations
    ZL_SDDL_OpCode_bit_and,
    ZL_SDDL_OpCode_bit_or,
    ZL_SDDL_OpCode_bit_xor,
    ZL_SDDL_OpCode_bit_not,

    // Logical operations
    ZL_SDDL_OpCode_log_and,
    ZL_SDDL_OpCode_log_or,
    ZL_SDDL_OpCode_log_not,

} ZL_SDDL_OpCode;

#define ZL_SDDL_OP_ARG_COUNT 2

typedef struct {
    ZL_SDDL_OpCode op;
    const ZL_SDDL_Expr* args[ZL_SDDL_OP_ARG_COUNT];
} ZL_SDDL_Op;

typedef struct {
    uint8_t _;
} ZL_SDDL_Null;

typedef enum {
    ZL_SDDL_ExprType_null,
    ZL_SDDL_ExprType_op,
    ZL_SDDL_ExprType_num,
    ZL_SDDL_ExprType_field,
    ZL_SDDL_ExprType_dest,
    ZL_SDDL_ExprType_var,
    ZL_SDDL_ExprType_scope,
    ZL_SDDL_ExprType_tuple,
    ZL_SDDL_ExprType_func,
} ZL_SDDL_ExprType;

struct ZL_SDDL_Expr_s {
    ZL_SDDL_ExprType type;
    union {
        ZL_SDDL_Null null;
        ZL_SDDL_Op op;
        ZL_SDDL_Number num;
        ZL_SDDL_Field field;
        ZL_SDDL_Dest dest;
        ZL_SDDL_Var var;
        ZL_SDDL_Scope* scope;
        ZL_SDDL_Tuple tuple;
        ZL_SDDL_Func func;
    };
    ZL_SDDL_SourceLocation loc;
};

ZL_RESULT_DECLARE_TYPE(ZL_SDDL_IntT);
ZL_RESULT_DECLARE_TYPE(ZL_SDDL_Expr);
ZL_RESULT_DECLARE_TYPE(ZL_SDDL_ExprType);
ZL_RESULT_DECLARE_TYPE(ZL_SDDL_FieldType);
ZL_RESULT_DECLARE_TYPE(ZL_SDDL_OpCode);

typedef const ZL_SDDL_Expr* ZL_SDDL_Expr_ConstPtr;
ZL_RESULT_DECLARE_TYPE(ZL_SDDL_Expr_ConstPtr);

/****************************
 * Expresssion Constructors *
 ****************************/

ZL_INLINE ZL_SDDL_Expr ZL_SDDL_Expr_makeNull(void)
{
    return (ZL_SDDL_Expr){
        .type = ZL_SDDL_ExprType_null,
        .null = { ._ = 0, },
    };
}

ZL_INLINE ZL_SDDL_Expr ZL_SDDL_Expr_makeNum(ZL_SDDL_IntT val)
{
    return (ZL_SDDL_Expr){
        .type = ZL_SDDL_ExprType_num,
        .num = {
            .val = val,
        },
    };
}

/*********
 * Utils *
 *********/

ZL_MAYBE_UNUSED_FUNCTION static const char* ZL_SDDL_FieldType_toString(
        ZL_SDDL_FieldType type)
{
    switch (type) {
        case ZL_SDDL_FieldType_poison:
            return "poison";
        case ZL_SDDL_FieldType_atom:
            return "atom";
        case ZL_SDDL_FieldType_record:
            return "record";
        case ZL_SDDL_FieldType_array:
            return "array";
        default:
            return "unknown";
    }
}

static const char* ZL_SDDL_OpCode_toString(ZL_SDDL_OpCode opcode)
{
    switch (opcode) {
        case ZL_SDDL_OpCode_die:
            return "die";
        case ZL_SDDL_OpCode_expect:
            return "expect";
        case ZL_SDDL_OpCode_log:
            return "log";
        case ZL_SDDL_OpCode_consume:
            return "consume";
        case ZL_SDDL_OpCode_sizeof:
            return "sizeof";
        case ZL_SDDL_OpCode_send:
            return "send";
        case ZL_SDDL_OpCode_assign:
            return "assign";
        case ZL_SDDL_OpCode_member:
            return "member";
        case ZL_SDDL_OpCode_bind:
            return "bind";
        case ZL_SDDL_OpCode_neg:
            return "neg";
        case ZL_SDDL_OpCode_eq:
            return "eq";
        case ZL_SDDL_OpCode_ne:
            return "ne";
        case ZL_SDDL_OpCode_gt:
            return "gt";
        case ZL_SDDL_OpCode_ge:
            return "ge";
        case ZL_SDDL_OpCode_lt:
            return "lt";
        case ZL_SDDL_OpCode_le:
            return "le";
        case ZL_SDDL_OpCode_add:
            return "add";
        case ZL_SDDL_OpCode_sub:
            return "sub";
        case ZL_SDDL_OpCode_mul:
            return "mul";
        case ZL_SDDL_OpCode_div:
            return "div";
        case ZL_SDDL_OpCode_mod:
            return "mod";
        case ZL_SDDL_OpCode_bit_and:
            return "bit_and";
        case ZL_SDDL_OpCode_bit_or:
            return "bit_or";
        case ZL_SDDL_OpCode_bit_xor:
            return "bit_xor";
        case ZL_SDDL_OpCode_bit_not:
            return "bit_not";
        case ZL_SDDL_OpCode_log_and:
            return "log_and";
        case ZL_SDDL_OpCode_log_or:
            return "log_or";
        case ZL_SDDL_OpCode_log_not:
            return "log_not";
        default:
            return "unknown";
    }
}

static const char* ZL_SDDL_ExprType_toString(ZL_SDDL_ExprType type)
{
    switch (type) {
        case ZL_SDDL_ExprType_null:
            return "null";
        case ZL_SDDL_ExprType_op:
            return "op";
        case ZL_SDDL_ExprType_num:
            return "num";
        case ZL_SDDL_ExprType_field:
            return "field";
        case ZL_SDDL_ExprType_dest:
            return "dest";
        case ZL_SDDL_ExprType_var:
            return "var";
        case ZL_SDDL_ExprType_scope:
            return "scope";
        case ZL_SDDL_ExprType_tuple:
            return "tuple";
        case ZL_SDDL_ExprType_func:
            return "func";
        default:
            return "unknown";
    }
}

static void log_expr(const ZL_SDDL_Expr* const expr)
{
    ZL_RLOG(ALWAYS,
            "Logging value of expr %p:\n  type: %s\n",
            expr,
            ZL_SDDL_ExprType_toString(expr->type));
    switch (expr->type) {
        case ZL_SDDL_ExprType_null:
            break;
        case ZL_SDDL_ExprType_op:
            ZL_RLOG(ALWAYS, "  op: %s\n", ZL_SDDL_OpCode_toString(expr->op.op));
            break;
        case ZL_SDDL_ExprType_num:
            ZL_RLOG(ALWAYS, "  val: %lld\n", (long long)expr->num.val);
            break;
        case ZL_SDDL_ExprType_field:
            break;
        case ZL_SDDL_ExprType_dest:
            break;
        case ZL_SDDL_ExprType_var:
            ZL_RLOG(ALWAYS,
                    "  name: '%.*s'\n",
                    expr->var.name.size,
                    expr->var.name.data);
            break;
        case ZL_SDDL_ExprType_scope:
            break;
        case ZL_SDDL_ExprType_tuple:
            break;
        case ZL_SDDL_ExprType_func:
            break;
        default:
            break;
    }
}

/***************************
 * Program Deserialization *
 ***************************/

struct ZL_SDDL_Program_s {
    Arena* arena;

    ZL_OperationContext local_opCtx;
    ZL_OperationContext* opCtx;

    uint32_t num_dests;

    const ZL_SDDL_Expr* root_exprs;
    size_t num_root_exprs;

    ZL_SDDL_SourceCode source_code;
};

ZL_SDDL_Program* ZL_SDDL_Program_create(ZL_OperationContext* opCtx)
{
    Arena* arena = ALLOC_HeapArena_create();
    if (arena == NULL) {
        return NULL;
    }
    ZL_SDDL_Program* const prog = (ZL_SDDL_Program*)ALLOC_Arena_malloc(
            arena, sizeof(ZL_SDDL_Program));
    if (prog == NULL) {
        ALLOC_Arena_freeArena(arena);
        return NULL;
    }
    memset(prog, 0, sizeof(*prog));
    prog->arena = arena;
    if (opCtx == NULL) {
        // Set up a local opCtx if one isn't provided.
        opCtx = &prog->local_opCtx;
        ZL_OC_init(opCtx);
    }
    prog->opCtx     = opCtx;
    prog->num_dests = 0;
    ZL_SDDL_SourceCode_initEmpty(arena, &prog->source_code);
    return prog;
}

void ZL_SDDL_Program_free(ZL_SDDL_Program* const prog)
{
    if (prog == NULL) {
        return;
    }
    ZL_SDDL_SourceCode_destroy(prog->arena, &prog->source_code);
    if (prog->opCtx == &prog->local_opCtx) {
        ZL_OC_destroy(prog->opCtx);
    }
    ALLOC_Arena_freeArena(prog->arena);
}

// Forward declarations
static ZL_Report ZL_SDDL_Program_decodeExpr_inner(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Expr* const expr,
        const A1C_Item* const item);
static ZL_RESULT_OF(ZL_SDDL_Expr_ConstPtr) ZL_SDDL_Program_decodeExpr(
        ZL_SDDL_Program* const prog,
        const A1C_Item* const item);

static size_t ZL_SDDL_OpCode_numArgs(const ZL_SDDL_OpCode opcode)
{
    switch (opcode) {
        case ZL_SDDL_OpCode_die:
            return 0;
        case ZL_SDDL_OpCode_expect:
        case ZL_SDDL_OpCode_log:
        case ZL_SDDL_OpCode_consume:
        case ZL_SDDL_OpCode_sizeof:
            return 1;
        case ZL_SDDL_OpCode_send:
        case ZL_SDDL_OpCode_assign:
        case ZL_SDDL_OpCode_member:
        case ZL_SDDL_OpCode_bind:
            return 2;
        case ZL_SDDL_OpCode_neg:
            return 1;
        case ZL_SDDL_OpCode_eq:
        case ZL_SDDL_OpCode_ne:
        case ZL_SDDL_OpCode_gt:
        case ZL_SDDL_OpCode_ge:
        case ZL_SDDL_OpCode_lt:
        case ZL_SDDL_OpCode_le:
        case ZL_SDDL_OpCode_add:
        case ZL_SDDL_OpCode_sub:
        case ZL_SDDL_OpCode_mul:
        case ZL_SDDL_OpCode_div:
        case ZL_SDDL_OpCode_mod:
            return 2;
        case ZL_SDDL_OpCode_bit_not:
            return 1;
        case ZL_SDDL_OpCode_bit_and:
        case ZL_SDDL_OpCode_bit_or:
        case ZL_SDDL_OpCode_bit_xor:
            return 2;
        case ZL_SDDL_OpCode_log_not:
            return 1;
        case ZL_SDDL_OpCode_log_and:
        case ZL_SDDL_OpCode_log_or:
            return 2;
        default:
            return 0;
    }
}

static ZL_Report ZL_SDDL_Program_decodeExpr_op(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Op* const op,
        const A1C_Item* const desc)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(prog->opCtx);
    const size_t expected_num_args = ZL_SDDL_OpCode_numArgs(op->op);
    if (expected_num_args == 0 && desc->type == A1C_ItemType_null) {
        // skip
    } else if (expected_num_args == 1 && desc->type == A1C_ItemType_map) {
        ZL_TRY_SET(
                ZL_SDDL_Expr_ConstPtr,
                op->args[0],
                ZL_SDDL_Program_decodeExpr(prog, desc));
    } else {
        A1C_TRY_EXTRACT_ARRAY(args, desc);
        ZL_ERR_IF_NE(args.size, expected_num_args, corruption);
        for (size_t i = 0; i < expected_num_args; i++) {
            ZL_TRY_SET(
                    ZL_SDDL_Expr_ConstPtr,
                    op->args[i],
                    ZL_SDDL_Program_decodeExpr(prog, &args.items[i]));
        }
    }
    for (size_t i = expected_num_args; i < ZL_SDDL_OP_ARG_COUNT; i++) {
        op->args[i] = NULL;
    }
    return ZL_returnSuccess();
}

static ZL_Report ZL_SDDL_Program_decodeExpr_num(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Number* const num,
        const A1C_Item* const desc)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(prog->opCtx);
    A1C_TRY_EXTRACT_INT64(val, desc);
    num->val = val;
    return ZL_returnSuccess();
}

static ZL_Report ZL_SDDL_Program_decodeExpr_field_poison(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Field_Poison* const poison,
        const A1C_Item* const desc)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(prog->opCtx);
    if (desc->type == A1C_ItemType_string) {
        A1C_TRY_EXTRACT_STRING(str, desc);
        ZL_TRY_SET(
                StringView,
                poison->msg,
                mk_sv_n(prog->arena, str.data, str.size));
    } else if (desc->type == A1C_ItemType_null) {
        poison->msg = StringView_init(NULL, 0);
    } else {
        ZL_ERR(corruption, "Unsupported description for poison field.");
    }
    return ZL_returnSuccess();
}

static ZL_Report ZL_SDDL_Program_decodeExpr_field_atom(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Field_Atom* const atom,
        const A1C_Item* const desc)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(prog->opCtx);

    if (desc->type == A1C_ItemType_string) {
        A1C_TRY_EXTRACT_STRING(str, desc);
        const StringView sv = StringView_initFromA1C(str);
        atom->width_expr    = NULL;
        if (StringView_eqCStr(&sv, "byte")) {
            atom->width         = 1;
            atom->type          = ZL_Type_serial;
            atom->is_integer    = true;
            atom->is_signed     = false;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "i1")) {
            atom->width         = 1;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = true;
            atom->is_signed     = true;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "u1")) {
            atom->width         = 1;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = true;
            atom->is_signed     = false;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "i2l")) {
            atom->width         = 2;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = true;
            atom->is_signed     = true;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "i2b")) {
            atom->width         = 2;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = true;
            atom->is_signed     = true;
            atom->is_big_endian = true;
        } else if (StringView_eqCStr(&sv, "u2l")) {
            atom->width         = 2;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = true;
            atom->is_signed     = false;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "u2b")) {
            atom->width         = 2;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = true;
            atom->is_signed     = false;
            atom->is_big_endian = true;
        } else if (StringView_eqCStr(&sv, "i4l")) {
            atom->width         = 4;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = true;
            atom->is_signed     = true;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "i4b")) {
            atom->width         = 4;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = true;
            atom->is_signed     = true;
            atom->is_big_endian = true;
        } else if (StringView_eqCStr(&sv, "u4l")) {
            atom->width         = 4;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = true;
            atom->is_signed     = false;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "u4b")) {
            atom->width         = 4;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = true;
            atom->is_signed     = false;
            atom->is_big_endian = true;
        } else if (StringView_eqCStr(&sv, "i8l")) {
            atom->width         = 8;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = true;
            atom->is_signed     = true;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "i8b")) {
            atom->width         = 8;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = true;
            atom->is_signed     = true;
            atom->is_big_endian = true;
        } else if (StringView_eqCStr(&sv, "u8l")) {
            atom->width         = 8;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = true;
            atom->is_signed     = false;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "u8b")) {
            atom->width         = 8;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = true;
            atom->is_signed     = false;
            atom->is_big_endian = true;
        } else if (StringView_eqCStr(&sv, "f1")) {
            atom->width         = 1;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = false;
            atom->is_signed     = true;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "f2l")) {
            atom->width         = 2;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = false;
            atom->is_signed     = true;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "f2b")) {
            atom->width         = 2;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = false;
            atom->is_signed     = true;
            atom->is_big_endian = true;
        } else if (StringView_eqCStr(&sv, "f4l")) {
            atom->width         = 4;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = false;
            atom->is_signed     = true;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "f4b")) {
            atom->width         = 4;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = false;
            atom->is_signed     = true;
            atom->is_big_endian = true;
        } else if (StringView_eqCStr(&sv, "f8l")) {
            atom->width         = 8;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = false;
            atom->is_signed     = true;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "f8b")) {
            atom->width         = 8;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = false;
            atom->is_signed     = true;
            atom->is_big_endian = true;
        } else if (StringView_eqCStr(&sv, "bf1")) {
            atom->width         = 1;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = false;
            atom->is_signed     = true;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "bf2l")) {
            atom->width         = 2;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = false;
            atom->is_signed     = true;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "bf2b")) {
            atom->width         = 2;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = false;
            atom->is_signed     = true;
            atom->is_big_endian = true;
        } else if (StringView_eqCStr(&sv, "bf4l")) {
            atom->width         = 4;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = false;
            atom->is_signed     = true;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "bf4b")) {
            atom->width         = 4;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = false;
            atom->is_signed     = true;
            atom->is_big_endian = true;
        } else if (StringView_eqCStr(&sv, "bf8l")) {
            atom->width         = 8;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = false;
            atom->is_signed     = true;
            atom->is_big_endian = false;
        } else if (StringView_eqCStr(&sv, "bf8b")) {
            atom->width         = 8;
            atom->type          = ZL_Type_numeric;
            atom->is_integer    = false;
            atom->is_signed     = true;
            atom->is_big_endian = true;
        } else {
            ZL_ERR(corruption,
                   "Unrecognized builtin type name: '%.*s'",
                   sv.size,
                   sv.data);
        }
    } else {
        ZL_TRY_SET(
                ZL_SDDL_Expr_ConstPtr,
                atom->width_expr,
                ZL_SDDL_Program_decodeExpr(prog, desc));

        // Evaluated at runtime.
        atom->width = 0;

        atom->type          = ZL_Type_serial;
        atom->is_signed     = false;
        atom->is_big_endian = false;
    }

    // Assigned in send op.
    atom->dest = (ZL_SDDL_Dest){
        .dest = 0,
    };

    // TODO: integer/float/struct? signedness? endianness?
    return ZL_returnSuccess();
}

static ZL_Report ZL_SDDL_Program_decodeExpr_field_record(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Field_Record* const record,
        const A1C_Item* const desc)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(prog->opCtx);
    A1C_TRY_EXTRACT_ARRAY(expr_list, desc);
    record->num_exprs = expr_list.size;
    record->exprs     = (const ZL_SDDL_Expr**)ALLOC_Arena_malloc(
            prog->arena, expr_list.size * sizeof(const ZL_SDDL_Expr*));
    record->dyn = NULL;
    ZL_ERR_IF_NULL(record->exprs, allocation);
    for (size_t i = 0; i < expr_list.size; i++) {
        ZL_TRY_SET(
                ZL_SDDL_Expr_ConstPtr,
                record->exprs[i],
                ZL_SDDL_Program_decodeExpr(prog, &expr_list.items[i]));
    }
    return ZL_returnSuccess();
}

static ZL_Report ZL_SDDL_Program_decodeExpr_field_array(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Field_Array* const array,
        const A1C_Item* const desc)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(prog->opCtx);
    A1C_TRY_EXTRACT_ARRAY(inner_items, desc);
    ZL_ERR_IF_NE(inner_items.size, 2, corruption); // expr and len
    const A1C_Item* const inner_expr_item = &inner_items.items[0];
    ZL_TRY_SET(
            ZL_SDDL_Expr_ConstPtr,
            array->expr,
            ZL_SDDL_Program_decodeExpr(prog, inner_expr_item));
    ZL_ERR_IF_NULL(array->expr, logicError);
    const A1C_Item* const len_item = &inner_items.items[1];
    ZL_TRY_SET(
            ZL_SDDL_Expr_ConstPtr,
            array->len,
            ZL_SDDL_Program_decodeExpr(prog, len_item));
    ZL_ERR_IF_NULL(array->len, logicError);
    array->dyn = NULL;
    // TODO: validate expr and len types?
    return ZL_returnSuccess();
}

static ZL_Report ZL_SDDL_Program_decodeExpr_field(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Field* const field,
        const A1C_Item* const desc)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(prog->opCtx);
    switch (field->type) {
        case ZL_SDDL_FieldType_poison:
            return ZL_SDDL_Program_decodeExpr_field_poison(
                    prog, &field->poison, desc);
        case ZL_SDDL_FieldType_atom:
            return ZL_SDDL_Program_decodeExpr_field_atom(
                    prog, &field->atom, desc);
        case ZL_SDDL_FieldType_record:
            return ZL_SDDL_Program_decodeExpr_field_record(
                    prog, &field->record, desc);
        case ZL_SDDL_FieldType_array:
            return ZL_SDDL_Program_decodeExpr_field_array(
                    prog, &field->array, desc);
        default:
            ZL_ERR(corruption, "Unknown field type %d!", field->type);
    }
}

static ZL_Report ZL_SDDL_Program_decodeExpr_dest(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Dest* const dest,
        const A1C_Item* const desc)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(prog->opCtx);
    ZL_ERR_IF_NE(desc->type, A1C_ItemType_null, corruption);

    dest->dest = prog->num_dests++;

    return ZL_returnSuccess();
}

static ZL_Report ZL_SDDL_Program_decodeExpr_var(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Var* const var,
        const A1C_Item* const desc)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(prog->opCtx);
    A1C_TRY_EXTRACT_STRING(name, desc);
    // TODO: validate name
    ZL_TRY_SET(
            StringView, var->name, mk_sv_n(prog->arena, name.data, name.size));
    return ZL_returnSuccess();
}

static ZL_Report ZL_SDDL_Program_decodeExpr_tuple(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Tuple* const tuple,
        const A1C_Item* const desc)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(prog->opCtx);
    A1C_TRY_EXTRACT_ARRAY(expr_items, desc);

    ZL_SDDL_Expr* const exprs = (ZL_SDDL_Expr*)ALLOC_Arena_malloc(
            prog->arena, expr_items.size * sizeof(ZL_SDDL_Expr));
    for (size_t i = 0; i < expr_items.size; i++) {
        ZL_ERR_IF_ERR(ZL_SDDL_Program_decodeExpr_inner(
                prog, &exprs[i], &expr_items.items[i]));
    }

    tuple->exprs     = exprs;
    tuple->num_exprs = expr_items.size;

    return ZL_returnSuccess();
}

static ZL_Report ZL_SDDL_Program_decodeExpr_func(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Func* const func,
        const A1C_Item* const desc)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(prog->opCtx);
    A1C_TRY_EXTRACT_ARRAY(arr, desc);

    ZL_ERR_IF_NE(arr.size, 2, corruption);

    A1C_TRY_EXTRACT_ARRAY(arg_items, &arr.items[0]);
    A1C_TRY_EXTRACT_ARRAY(expr_items, &arr.items[1]);

    ZL_SDDL_Var* const args = (ZL_SDDL_Var*)ALLOC_Arena_malloc(
            prog->arena, arg_items.size * sizeof(ZL_SDDL_Var));
    ZL_ERR_IF_NULL(args, allocation);

    ZL_SDDL_Expr* const exprs = (ZL_SDDL_Expr*)ALLOC_Arena_malloc(
            prog->arena, expr_items.size * sizeof(ZL_SDDL_Expr));
    ZL_ERR_IF_NULL(exprs, allocation);

    for (size_t i = 0; i < arg_items.size; i++) {
        ZL_SDDL_Expr var_expr;
        ZL_ERR_IF_ERR(ZL_SDDL_Program_decodeExpr_inner(
                prog, &var_expr, &arg_items.items[i]));
        ZL_ERR_IF_NE(var_expr.type, ZL_SDDL_ExprType_var, corruption);
        args[i] = var_expr.var;
    }

    for (size_t i = 0; i < expr_items.size; i++) {
        ZL_ERR_IF_ERR(ZL_SDDL_Program_decodeExpr_inner(
                prog, &exprs[i], &expr_items.items[i]));
    }

    func->unbound_args     = args;
    func->num_unbound_args = arg_items.size;

    func->exprs     = exprs;
    func->num_exprs = expr_items.size;

    func->scope = NULL;

    return ZL_returnSuccess();
}

// Doesn't initialize fields not present in the debug info, because it expects
// ZL_SDDL_Program_decodeExpr_clearDebugInfo() to have been unconditionally
// called on the expr already, which default-inits everything.
static ZL_Report ZL_SDDL_Program_decodeExpr_addDebugInfo(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Expr* const expr,
        const A1C_Item* const dbg_item)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(prog->opCtx);
    A1C_TRY_EXTRACT_MAP(dbg_map, dbg_item);

    const A1C_Item* const loc_item = A1C_Map_get_cstr(&dbg_map, "loc");
    if (loc_item != NULL) {
        A1C_TRY_EXTRACT_ARRAY(loc_array, loc_item);
        ZL_ERR_IF_NE(loc_array.size, 2, corruption);
        A1C_TRY_EXTRACT_INT64(start, &loc_array.items[0]);
        A1C_TRY_EXTRACT_INT64(size, &loc_array.items[1]);
        ZL_ERR_IF_LT(start, 0, corruption);
        ZL_ERR_IF_LT(size, 0, corruption);
        expr->loc.start = (size_t)start;
        expr->loc.size  = (size_t)size;
    }

    return ZL_returnSuccess();
}

static void ZL_SDDL_Program_decodeExpr_clearDebugInfo(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Expr* const expr)
{
    (void)prog;
    memset(&expr->loc, 0, sizeof(expr->loc));
}

static ZL_Report ZL_SDDL_Program_decodeExprType(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Expr* const expr,
        const A1C_Item* const type_item)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(prog->opCtx);
    A1C_TRY_EXTRACT_STRING(type_str, type_item);
    const StringView type_sv = StringView_initFromA1C(type_str);

    // Ops
    if (StringView_eqCStr(&type_sv, "die")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_die;
    } else if (StringView_eqCStr(&type_sv, "expect")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_expect;
    } else if (StringView_eqCStr(&type_sv, "log")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_log;
    } else if (StringView_eqCStr(&type_sv, "consume")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_consume;
    } else if (StringView_eqCStr(&type_sv, "sizeof")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_sizeof;
    } else if (StringView_eqCStr(&type_sv, "send")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_send;
    } else if (StringView_eqCStr(&type_sv, "assign")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_assign;
    } else if (StringView_eqCStr(&type_sv, "member")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_member;
    } else if (StringView_eqCStr(&type_sv, "bind")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_bind;
    } else if (StringView_eqCStr(&type_sv, "neg")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_neg;
    } else if (StringView_eqCStr(&type_sv, "eq")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_eq;
    } else if (StringView_eqCStr(&type_sv, "ne")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_ne;
    } else if (StringView_eqCStr(&type_sv, "gt")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_gt;
    } else if (StringView_eqCStr(&type_sv, "ge")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_ge;
    } else if (StringView_eqCStr(&type_sv, "lt")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_lt;
    } else if (StringView_eqCStr(&type_sv, "le")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_le;
    } else if (StringView_eqCStr(&type_sv, "add")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_add;
    } else if (StringView_eqCStr(&type_sv, "sub")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_sub;
    } else if (StringView_eqCStr(&type_sv, "mul")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_mul;
    } else if (StringView_eqCStr(&type_sv, "div")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_div;
    } else if (StringView_eqCStr(&type_sv, "mod")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_mod;
    } else if (StringView_eqCStr(&type_sv, "bit_and")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_bit_and;
    } else if (StringView_eqCStr(&type_sv, "bit_or")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_bit_or;
    } else if (StringView_eqCStr(&type_sv, "bit_xor")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_bit_xor;
    } else if (StringView_eqCStr(&type_sv, "bit_not")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_bit_not;
    } else if (StringView_eqCStr(&type_sv, "log_and")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_log_and;
    } else if (StringView_eqCStr(&type_sv, "log_or")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_log_or;
    } else if (StringView_eqCStr(&type_sv, "log_not")) {
        expr->type  = ZL_SDDL_ExprType_op;
        expr->op.op = ZL_SDDL_OpCode_log_not;
    }
    // Num
    else if (StringView_eqCStr(&type_sv, "int")) {
        expr->type = ZL_SDDL_ExprType_num;
    }
    // Fields
    else if (StringView_eqCStr(&type_sv, "poison")) {
        expr->type       = ZL_SDDL_ExprType_field;
        expr->field.type = ZL_SDDL_FieldType_poison;
    } else if (StringView_eqCStr(&type_sv, "atom")) {
        expr->type       = ZL_SDDL_ExprType_field;
        expr->field.type = ZL_SDDL_FieldType_atom;
    } else if (StringView_eqCStr(&type_sv, "record")) {
        expr->type       = ZL_SDDL_ExprType_field;
        expr->field.type = ZL_SDDL_FieldType_record;
    } else if (StringView_eqCStr(&type_sv, "array")) {
        expr->type       = ZL_SDDL_ExprType_field;
        expr->field.type = ZL_SDDL_FieldType_array;
    }
    // Dests
    else if (StringView_eqCStr(&type_sv, "dest")) {
        expr->type = ZL_SDDL_ExprType_dest;
    }
    // Var
    else if (StringView_eqCStr(&type_sv, "var")) {
        expr->type = ZL_SDDL_ExprType_var;
    }
    // Tuple
    else if (StringView_eqCStr(&type_sv, "tuple")) {
        expr->type = ZL_SDDL_ExprType_tuple;
    }
    // Func
    else if (StringView_eqCStr(&type_sv, "func")) {
        expr->type = ZL_SDDL_ExprType_func;
    } else {
        ZL_ERR(corruption,
               "Unknown expression type '%.*s'.",
               type_sv.size,
               type_sv.data);
    }

    return ZL_returnSuccess();
}

static ZL_Report ZL_SDDL_Program_decodeExpr_inner(
        ZL_SDDL_Program* const prog,
        ZL_SDDL_Expr* const expr,
        const A1C_Item* const item)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(prog->opCtx);
    A1C_TRY_EXTRACT_MAP(item_map, item);
    ZL_ERR_IF_LT(item_map.size, 1, corruption);
    ZL_ERR_IF_GT(item_map.size, 2, corruption);
    const A1C_Pair* const pair = &item_map.items[0];
    const A1C_Item* const key  = &pair->key;
    const A1C_Item* const val  = &pair->val;

    ZL_ERR_IF_ERR(ZL_SDDL_Program_decodeExprType(prog, expr, key));

    switch (expr->type) {
        case ZL_SDDL_ExprType_null:
            ZL_ERR(corruption, "Can't decode an expression to null type!");
        case ZL_SDDL_ExprType_op:
            ZL_ERR_IF_ERR(ZL_SDDL_Program_decodeExpr_op(prog, &expr->op, val));
            break;
        case ZL_SDDL_ExprType_num:
            ZL_ERR_IF_ERR(
                    ZL_SDDL_Program_decodeExpr_num(prog, &expr->num, val));
            break;
        case ZL_SDDL_ExprType_field:
            ZL_ERR_IF_ERR(
                    ZL_SDDL_Program_decodeExpr_field(prog, &expr->field, val));
            break;
        case ZL_SDDL_ExprType_dest:
            ZL_ERR_IF_ERR(
                    ZL_SDDL_Program_decodeExpr_dest(prog, &expr->dest, val));
            break;
        case ZL_SDDL_ExprType_var:
            ZL_ERR_IF_ERR(
                    ZL_SDDL_Program_decodeExpr_var(prog, &expr->var, val));
            break;
        case ZL_SDDL_ExprType_scope:
            ZL_ERR(corruption,
                   "Scopes can't be represented in the program, they only exist during execution.");
        case ZL_SDDL_ExprType_tuple:
            ZL_ERR_IF_ERR(
                    ZL_SDDL_Program_decodeExpr_tuple(prog, &expr->tuple, val));
            break;
        case ZL_SDDL_ExprType_func:
            ZL_ERR_IF_ERR(
                    ZL_SDDL_Program_decodeExpr_func(prog, &expr->func, val));
            break;
        default:
            ZL_ERR(corruption, "Unknown expression type %d!", expr->type);
    }

    ZL_SDDL_Program_decodeExpr_clearDebugInfo(prog, expr);

    if (item_map.size == 2) {
        const A1C_Pair* const dbg_pair = &item_map.items[1];
        const A1C_Item* const dbg_key  = &dbg_pair->key;
        const A1C_Item* const dbg_val  = &dbg_pair->val;

        A1C_TRY_EXTRACT_STRING(dbg_key_str, dbg_key);
        const StringView dbg_key_sv = StringView_initFromA1C(dbg_key_str);
        ZL_ERR_IF(!StringView_eqCStr(&dbg_key_sv, "dbg"), corruption);
        ZL_ERR_IF_ERR(
                ZL_SDDL_Program_decodeExpr_addDebugInfo(prog, expr, dbg_val));
    }

    return ZL_returnSuccess();
}

/**
 * Wrapper that allocates an expr before invoking the inner func to fill it in.
 */
static ZL_RESULT_OF(ZL_SDDL_Expr_ConstPtr) ZL_SDDL_Program_decodeExpr(
        ZL_SDDL_Program* const prog,
        const A1C_Item* const item)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr_ConstPtr, prog->opCtx);

    ZL_SDDL_Expr* const expr =
            ALLOC_Arena_malloc(prog->arena, sizeof(ZL_SDDL_Expr));
    ZL_ERR_IF_NULL(expr, allocation);

    ZL_ERR_IF_ERR(ZL_SDDL_Program_decodeExpr_inner(prog, expr, item));

    return ZL_WRAP_VALUE(expr);
}

ZL_Report ZL_SDDL_Program_load(
        ZL_SDDL_Program* const prog,
        const void* const src,
        const size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(NULL);
    ZL_ERR_IF_NULL(prog, parameter_invalid);
    ZL_RESULT_UPDATE_SCOPE_CONTEXT(prog->opCtx);

    ZL_ERR_IF_NULL(src, parameter_invalid);

    const A1C_Item* root_item;
    {
        const A1C_Arena a1c_arena = A1C_Arena_wrap(prog->arena);
        const A1C_DecoderConfig decoder_config =
                (A1C_DecoderConfig){ .maxDepth            = 0,
                                     .limitBytes          = 0,
                                     .referenceSource     = true,
                                     .rejectUnknownSimple = true };
        A1C_Decoder decoder;
        A1C_Decoder_init(&decoder, a1c_arena, decoder_config);

        root_item = A1C_Decoder_decode(&decoder, (const uint8_t*)src, srcSize);
        if (root_item == NULL) {
            return ZL_WRAP_ERROR(A1C_Error_convert(
                    ZL_ERR_CTX_PTR, A1C_Decoder_getError(&decoder)));
        }
    }

    A1C_TRY_EXTRACT_MAP(root_map, root_item);

    {
        const A1C_Item* const src_item = A1C_Map_get_cstr(&root_map, "src");
        if (src_item != NULL) {
            A1C_TRY_EXTRACT_STRING(src_str, src_item);
            ZL_SDDL_SourceCode_init(
                    prog->arena,
                    &prog->source_code,
                    StringView_initFromA1C(src_str));
        } else {
            ZL_SDDL_SourceCode_initEmpty(prog->arena, &prog->source_code);
        }
    }

    {
        A1C_TRY_EXTRACT_ARRAY(expr_array, A1C_Map_get_cstr(&root_map, "exprs"));

        ZL_SDDL_Expr* const root_exprs = (ZL_SDDL_Expr*)ALLOC_Arena_malloc(
                prog->arena, expr_array.size * sizeof(ZL_SDDL_Expr));
        ZL_ERR_IF_NULL(root_exprs, allocation);
        for (size_t i = 0; i < expr_array.size; i++) {
            ZL_ERR_IF_ERR(ZL_SDDL_Program_decodeExpr_inner(
                    prog, &root_exprs[i], &expr_array.items[i]));
        }
        prog->root_exprs     = (const ZL_SDDL_Expr*)root_exprs;
        prog->num_root_exprs = expr_array.size;
    }

    return ZL_returnSuccess();
}

const char* ZL_SDDL_Program_getErrorContextString_fromError(
        const ZL_SDDL_Program* prog,
        ZL_Error error)
{
    if (prog == NULL) {
        return NULL;
    }
    if (!ZL_E_isError(error)) {
        return NULL;
    }
    return ZL_OC_getErrorContextString(prog->opCtx, error);
}

/********************
 * State Management *
 ********************/

DECLARE_VECTOR_TYPE(ZL_SDDL_OutputInfo)

struct ZL_SDDL_State_s {
    Arena* arena;

    ZL_OperationContext local_opCtx;
    ZL_OperationContext* opCtx;

    const ZL_SDDL_Program* prog;

    // Variables
    ZL_SDDL_Scope* globals;

    // Dests
    VECTOR(ZL_SDDL_OutputInfo) dests;

    // Tags
    uint32_t num_tags;
    VECTOR(size_t) segment_sizes;
    VECTOR(uint32_t) segment_tags;

    // Input
    const uint8_t* src;
    size_t size;
    size_t pos;

    // Correctness Validation:
    size_t num_dyn_expr_sets_creates;
    size_t num_dyn_expr_sets_destroys;

    size_t num_scopes_creates;
    size_t num_scopes_destroys;

    ZL_SDDL_SourceLocation cur_src_loc;
};

// Forward declaration
static void ZL_SDDL_Expr_incref(
        ZL_SDDL_State* const state,
        ZL_SDDL_Expr* const expr);
static void ZL_SDDL_Expr_decref(
        ZL_SDDL_State* const state,
        ZL_SDDL_Expr* const expr);

// Scope Methods

static ZL_SDDL_Scope* ZL_SDDL_Scope_create(ZL_SDDL_State* const state)
{
    ZL_SDDL_Scope* const scope = (ZL_SDDL_Scope*)ALLOC_Arena_malloc(
            state->arena, sizeof(ZL_SDDL_Scope));
    if (scope != NULL) {
        state->num_scopes_creates++;
        scope->refs = ZL_SDDL_RefCount_create();
        scope->vars = ZL_SDDL_VarMap_createInArena(
                state->arena, ZL_SDDL_VARIABLE_LIMIT);
    }
    return scope;
}

ZL_INLINE ZL_SDDL_Expr ZL_SDDL_Expr_makeScope(ZL_SDDL_Scope* const scope)
{
    return (ZL_SDDL_Expr){
        .type  = ZL_SDDL_ExprType_scope,
        .scope = scope,
    };
}

static void ZL_SDDL_Scope_clear(
        ZL_SDDL_State* const state,
        ZL_SDDL_Scope* const scope)
{
    (void)state;

    ZL_SDDL_VarMap_IterMut it = ZL_SDDL_VarMap_iterMut(&scope->vars);
    ZL_SDDL_VarMap_Entry* entry;
    while ((entry = ZL_SDDL_VarMap_IterMut_next(&it)) != NULL) {
        if (entry->val != NULL) {
            ZL_SDDL_Expr_decref(state, entry->val);
            ALLOC_Arena_free(state->arena, entry->val);
        }
    }
    ZL_SDDL_VarMap_clear(&scope->vars);
}

static void ZL_SDDL_Scope_free(
        ZL_SDDL_State* const state,
        ZL_SDDL_Scope* const scope)
{
    if (scope == NULL) {
        return;
    }
    state->num_scopes_destroys++;

    ZL_SDDL_Scope_clear(state, scope);
    ZL_SDDL_VarMap_destroy(&scope->vars);
    ZL_SDDL_RefCount_destroy(&scope->refs);
    ALLOC_Arena_free(state->arena, scope);
}

static void ZL_SDDL_Scope_free_wrapper(void* const scope, void* const state)
{
    ZL_SDDL_Scope_free((ZL_SDDL_State*)state, (ZL_SDDL_Scope*)scope);
}

static void ZL_SDDL_Scope_incref(
        ZL_SDDL_State* const state,
        ZL_SDDL_Scope* const scope)
{
    (void)state;
    ZL_SDDL_RefCount_incref(&scope->refs);
}

static void ZL_SDDL_Scope_decref(
        ZL_SDDL_State* const state,
        ZL_SDDL_Scope* const scope)
{
    ZL_SDDL_RefCount_decref(
            &scope->refs, ZL_SDDL_Scope_free_wrapper, scope, state);
}

static ZL_SDDL_Scope* ZL_SDDL_Scope_createCopy(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Scope* const src)
{
    ZL_SDDL_Scope* const dst = ZL_SDDL_Scope_create(state);
    if (dst == NULL) {
        return NULL;
    }

    if (src != NULL) {
        ZL_SDDL_VarMap_Iter it = ZL_SDDL_VarMap_iter(&src->vars);
        const ZL_SDDL_VarMap_Entry* entryPtr;
        while ((entryPtr = ZL_SDDL_VarMap_Iter_next(&it)) != NULL) {
            ZL_SDDL_VarMap_Entry entry;
            entry.key = entryPtr->key;
            entry.val = (ZL_SDDL_Expr*)ALLOC_Arena_malloc(
                    state->arena, sizeof(ZL_SDDL_Expr));
            if (entry.val == NULL) {
                ZL_SDDL_Scope_free(state, dst);
                return NULL;
            }
            *entry.val = *entryPtr->val;
            const ZL_SDDL_VarMap_Insert insert =
                    ZL_SDDL_VarMap_insert(&dst->vars, &entry);
            if (!insert.inserted) {
                ZL_SDDL_Scope_free(state, dst);
                return NULL;
            }
            ZL_SDDL_Expr_incref(state, insert.ptr->val);
        }
    }

    return dst;
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_Scope_get(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Scope* const scope,
        const ZL_SDDL_Var* const var)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);

    const ZL_SDDL_VarMap_Entry* entry =
            ZL_SDDL_VarMap_find(&scope->vars, &var->name);
    ZL_ERR_IF_NULL(
            entry,
            corruption,
            "Variable '%.*s' read without ever having been written.",
            var->name.size,
            var->name.data);
    ZL_ERR_IF_NULL(
            entry->val,
            corruption,
            "Variable '%.*s' has NULL value.",
            var->name.size,
            var->name.data);
    ZL_SDDL_Expr result = *entry->val;
    ZL_SDDL_Expr_incref(state, &result);
    return ZL_WRAP_VALUE(result);
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_Scope_set(
        ZL_SDDL_State* const state,
        ZL_SDDL_Scope* const scope,
        const ZL_SDDL_Var* const var,
        const ZL_SDDL_Expr* const val)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);

    ZL_ERR_IF_EQ(
            val->type,
            ZL_SDDL_ExprType_var,
            corruption,
            "Expression being assigned to variable cannot itself be an unresolved variable!");

    // ZL_LOG(ALWAYS, "Setting var '%.*s' to val:", var->name.size,
    // var->name.data); log_expr(val);

    const ZL_SDDL_VarMap_Entry entry = {
        .key = var->name,
        .val = NULL,
    };
    const ZL_SDDL_VarMap_Insert insert =
            ZL_SDDL_VarMap_insert(&scope->vars, &entry);
    ZL_ERR_IF(insert.badAlloc, allocation);
    if (insert.inserted) {
        insert.ptr->val = (ZL_SDDL_Expr*)ALLOC_Arena_malloc(
                state->arena, sizeof(ZL_SDDL_Expr));
        ZL_ERR_IF_NULL(insert.ptr->val, allocation);
    } else {
        ZL_SDDL_Expr_decref(state, insert.ptr->val);
    }
    *insert.ptr->val = *val;
    ZL_SDDL_Expr_incref(state, insert.ptr->val);
    return ZL_WRAP_VALUE(*val);
}

// Cached Instructions Methods

struct ZL_SDDL_CachedInstructions_s {
    size_t* lens;
    uint32_t* tags;
    size_t count;
    size_t total_size;
};

static ZL_SDDL_CachedInstructions* ZL_SDDL_CachedInstructions_create(
        ZL_SDDL_State* const state,
        const uint32_t* const tags,
        const size_t* const lens,
        const size_t count,
        const size_t total_size,
        const size_t first_instr_len_offset)
{
    const size_t size_needed = sizeof(ZL_SDDL_CachedInstructions)
            + count * sizeof(size_t) + count * sizeof(uint32_t);
    char* buf = ALLOC_Arena_malloc(state->arena, size_needed);
    if (buf == NULL) {
        return NULL;
    }
    ZL_SDDL_CachedInstructions* const instrs =
            (ZL_SDDL_CachedInstructions*)(void*)buf;
    buf += sizeof(ZL_SDDL_CachedInstructions);
    instrs->lens = (size_t*)(void*)buf;
    buf += count * sizeof(size_t);
    instrs->tags       = (uint32_t*)(void*)buf;
    instrs->count      = count;
    instrs->total_size = total_size;

    memcpy(instrs->lens, lens, count * sizeof(size_t));
    memcpy(instrs->tags, tags, count * sizeof(uint32_t));

    if (first_instr_len_offset) {
        instrs->lens[0] -= first_instr_len_offset;
    }

    return instrs;
}

static void ZL_SDDL_CachedInstructions_free(
        ZL_SDDL_State* const state,
        ZL_SDDL_CachedInstructions* const instrs)
{
    if (instrs == NULL) {
        return;
    }
    ALLOC_Arena_free(state->arena, instrs);
}

// DynExprSet Methods

struct ZL_SDDL_DynExprSet_s {
    ZL_SDDL_RefCount refs;

    // Points at expressions owned by this expr set.
    ZL_SDDL_Expr* exprs;
    size_t num_exprs;

    // Possibly null pointers corresponding to the names the expression results
    // should be assigned to (only applies to records). The pointed-to vars are
    // not owned by the expr set.
    const ZL_SDDL_Var** names;

    ZL_SDDL_CachedInstructions* instrs;
};

static ZL_SDDL_DynExprSet* ZL_SDDL_DynExprSet_create(
        ZL_SDDL_State* const state,
        const size_t num_exprs,
        bool with_names)
{
    const size_t bufsize = sizeof(ZL_SDDL_DynExprSet)
            + num_exprs * sizeof(ZL_SDDL_Expr)
            + num_exprs * with_names * sizeof(ZL_SDDL_Var*);
    char* buf = ALLOC_Arena_malloc(state->arena, bufsize);
    if (buf == NULL) {
        return NULL;
    }
    state->num_dyn_expr_sets_creates++;
    ZL_SDDL_DynExprSet* const exprset = (ZL_SDDL_DynExprSet*)(void*)buf;
    buf += sizeof(ZL_SDDL_DynExprSet);
    exprset->refs  = ZL_SDDL_RefCount_create();
    exprset->exprs = (ZL_SDDL_Expr*)(void*)buf;
    buf += num_exprs * sizeof(ZL_SDDL_Expr);
    exprset->num_exprs = num_exprs;
    if (with_names) {
        exprset->names = (const ZL_SDDL_Var**)(void*)buf;
        buf += num_exprs * sizeof(ZL_SDDL_Var*);
        memset(exprset->names, 0, num_exprs * sizeof(ZL_SDDL_Var*));
    } else {
        exprset->names = NULL;
    }
    exprset->instrs = NULL;
    ZL_ASSERT_EQ(buf - (char*)exprset, bufsize);
    return exprset;
}

static void ZL_SDDL_DynExprSet_destroy(
        ZL_SDDL_State* const state,
        ZL_SDDL_DynExprSet* const exprset)
{
    // ZL_LOG(ALWAYS, "destroy %p with %zu exprs", exprset, exprset->num_exprs);
    ZL_SDDL_RefCount_destroy(&exprset->refs);
    state->num_dyn_expr_sets_destroys++;
    ZL_SDDL_CachedInstructions_free(state, exprset->instrs);
    for (size_t i = 0; i < exprset->num_exprs; i++) {
        ZL_SDDL_Expr_decref(state, &exprset->exprs[i]);
    }
    ALLOC_Arena_free(state->arena, exprset);
}

static void ZL_SDDL_DynExprSet_destroy_wrapper(
        void* const exprset,
        void* const state)
{
    ZL_SDDL_DynExprSet_destroy(
            (ZL_SDDL_State*)state, (ZL_SDDL_DynExprSet*)exprset);
}

static void ZL_SDDL_DynExprSet_incref(
        ZL_SDDL_State* const state,
        ZL_SDDL_DynExprSet* const exprset)
{
    (void)state;
    ZL_SDDL_RefCount_incref(&exprset->refs);
}

static void ZL_SDDL_DynExprSet_decref(
        ZL_SDDL_State* const state,
        ZL_SDDL_DynExprSet* const exprset)
{
    ZL_SDDL_RefCount_decref(
            &exprset->refs, ZL_SDDL_DynExprSet_destroy_wrapper, exprset, state);
}

static void ZL_SDDL_Field_incref(
        ZL_SDDL_State* const state,
        ZL_SDDL_Field* const field)
{
    switch (field->type) {
        case ZL_SDDL_FieldType_poison:
        case ZL_SDDL_FieldType_atom:
            break;
        case ZL_SDDL_FieldType_record: {
            ZL_SDDL_DynExprSet* const exprset = field->record.dyn;
            if (exprset != NULL) {
                ZL_SDDL_DynExprSet_incref(state, exprset);
            }
            break;
        }
        case ZL_SDDL_FieldType_array: {
            ZL_SDDL_DynExprSet* const exprset = field->array.dyn;
            if (exprset != NULL) {
                ZL_SDDL_DynExprSet_incref(state, exprset);
            }
            break;
        }
    }
}

static void ZL_SDDL_Expr_incref(
        ZL_SDDL_State* const state,
        ZL_SDDL_Expr* const expr)
{
    // ZL_LOG(ALWAYS, "incref %p", expr);
    switch (expr->type) {
        case ZL_SDDL_ExprType_field:
            ZL_SDDL_Field_incref(state, &expr->field);
            break;
        case ZL_SDDL_ExprType_null:
        case ZL_SDDL_ExprType_op:
        case ZL_SDDL_ExprType_num:
        case ZL_SDDL_ExprType_dest:
        case ZL_SDDL_ExprType_var:
            break;
        case ZL_SDDL_ExprType_scope:
            ZL_SDDL_Scope_incref(state, expr->scope);
            break;
        case ZL_SDDL_ExprType_tuple:
            break;
        case ZL_SDDL_ExprType_func:
            if (expr->func.scope != NULL) {
                ZL_SDDL_Scope_incref(state, expr->func.scope);
            }
            break;
    }
}

static void ZL_SDDL_Field_decref(
        ZL_SDDL_State* const state,
        ZL_SDDL_Field* const field)
{
    switch (field->type) {
        case ZL_SDDL_FieldType_poison:
        case ZL_SDDL_FieldType_atom:
            break;
        case ZL_SDDL_FieldType_record: {
            ZL_SDDL_DynExprSet* const exprset = field->record.dyn;
            if (exprset != NULL) {
                ZL_SDDL_DynExprSet_decref(state, exprset);
            }
            break;
        }
        case ZL_SDDL_FieldType_array: {
            ZL_SDDL_DynExprSet* const exprset = field->array.dyn;
            if (exprset != NULL) {
                ZL_SDDL_DynExprSet_decref(state, exprset);
            }
            break;
        }
    }
}

static void ZL_SDDL_Expr_decref(
        ZL_SDDL_State* const state,
        ZL_SDDL_Expr* const expr)
{
    // ZL_LOG(ALWAYS, "decref %p", expr);
    switch (expr->type) {
        case ZL_SDDL_ExprType_field:
            ZL_SDDL_Field_decref(state, &expr->field);
            break;
        case ZL_SDDL_ExprType_null:
        case ZL_SDDL_ExprType_op:
        case ZL_SDDL_ExprType_num:
        case ZL_SDDL_ExprType_dest:
        case ZL_SDDL_ExprType_var:
            break;
        case ZL_SDDL_ExprType_scope:
            ZL_SDDL_Scope_decref(state, expr->scope);
            break;
        case ZL_SDDL_ExprType_tuple:
            break;
        case ZL_SDDL_ExprType_func:
            if (expr->func.scope != NULL) {
                ZL_SDDL_Scope_decref(state, expr->func.scope);
            }
            break;
    }
}

ZL_SDDL_State* ZL_SDDL_State_create(
        const ZL_SDDL_Program* const prog,
        ZL_OperationContext* opCtx)
{
    if (prog == NULL) {
        return NULL;
    }

    Arena* arena = ALLOC_HeapArena_create();
    if (arena == NULL) {
        return NULL;
    }
    ZL_SDDL_State* const state =
            (ZL_SDDL_State*)ALLOC_Arena_malloc(arena, sizeof(ZL_SDDL_State));
    if (state == NULL) {
        ALLOC_Arena_freeArena(arena);
        return NULL;
    }
    memset(state, 0, sizeof(*state));
    state->arena = arena;

    if (opCtx == NULL) {
        // Set up a local opCtx if one isn't provided.
        opCtx = &state->local_opCtx;
        ZL_OC_init(opCtx);
    }
    state->opCtx = opCtx;

    state->num_dyn_expr_sets_creates  = 0;
    state->num_dyn_expr_sets_destroys = 0;
    state->num_scopes_creates         = 0;
    state->num_scopes_destroys        = 0;

    state->prog = prog;

    state->globals = ZL_SDDL_Scope_create(state);
    if (state->globals == NULL) {
        ZL_SDDL_State_free(state);
        return NULL;
    }

    VECTOR_INIT_IN_ARENA(state->dests, arena, ZL_SDDL_DEST_LIMIT);

    state->num_tags = prog->num_dests;
    VECTOR_INIT_IN_ARENA(state->segment_sizes, arena, ZL_SDDL_SEGMENT_LIMIT);
    VECTOR_INIT_IN_ARENA(state->segment_tags, arena, ZL_SDDL_SEGMENT_LIMIT);

    state->cur_src_loc.start = 0;
    state->cur_src_loc.size  = 0;

    return state;
}

void ZL_SDDL_State_free(ZL_SDDL_State* const state)
{
    if (state == NULL) {
        return;
    }

    VECTOR_DESTROY(state->segment_tags);
    VECTOR_DESTROY(state->segment_sizes);

    VECTOR_DESTROY(state->dests);

    ZL_ASSERT_EQ(ZL_SDDL_RefCount_count(&state->globals->refs), 1);
    ZL_SDDL_Scope_decref(state, state->globals);

    if (state->opCtx == &state->local_opCtx) {
        ZL_OC_destroy(state->opCtx);
    }

    ALLOC_Arena_freeArena(state->arena);
}

// Forward declaration
static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_execExpr(
        ZL_SDDL_State* const state,
        ZL_SDDL_Scope* const scope,
        const ZL_SDDL_Expr* const expr);

// Forward declaration
static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_consume(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Expr* const expr);

// Forward declaration
static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_consumeField(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Field* const field);

static ZL_Report ZL_SDDL_State_updateDest(
        ZL_SDDL_State* const state,
        const uint32_t tag,
        const ZL_SDDL_Field_Atom* const atom)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->opCtx);

    if (ZL_UNLIKELY(tag >= VECTOR_SIZE(state->dests))) {
        const size_t old_size = VECTOR_SIZE(state->dests);
        ZL_ERR_IF(!VECTOR_RESIZE(state->dests, tag + 1), allocation);
        memset(&VECTOR_AT(state->dests, old_size),
               0,
               (tag - old_size) * sizeof(VECTOR_AT(state->dests, 0)));
    }

    ZL_SDDL_OutputInfo* const oi = &VECTOR_AT(state->dests, tag);

    if (oi->width == 0) {
        oi->type       = atom->type;
        oi->width      = atom->width;
        oi->big_endian = atom->is_big_endian;
    } else {
        ZL_ERR_IF_NE(
                oi->type,
                atom->type,
                GENERIC,
                "Can't send different types to the same dest.");
        if (atom->type != ZL_Type_serial) {
            ZL_ERR_IF_NE(
                    oi->width,
                    atom->width,
                    GENERIC,
                    "Can't mix fields of different widths in the same dest.");
            ZL_ERR_IF_NE(
                    oi->big_endian,
                    atom->is_big_endian,
                    GENERIC,
                    "Can't mix fields of different endianness in the same dest.");
        }
    }

    return ZL_returnSuccess();
}

static ZL_SDDL_Expr ZL_SDDL_State_readAtom(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Field_Atom* const atom)
{
    switch (atom->type) {
        case ZL_Type_serial: {
            ZL_ASSERT_EQ(atom->width, 1);
            return ZL_SDDL_Expr_makeNum(
                    (ZL_SDDL_IntT)ZL_read8(state->src + state->pos));
        }
        case ZL_Type_numeric:
            if (atom->is_integer) {
                switch (atom->width) {
                    case 1: {
                        uint8_t narrow = ZL_read8(state->src + state->pos);
                        if (atom->is_signed) {
                            return ZL_SDDL_Expr_makeNum(
                                    (ZL_SDDL_IntT)(int8_t)narrow);
                        } else {
                            return ZL_SDDL_Expr_makeNum((ZL_SDDL_IntT)narrow);
                        }
                    }
                    case 2: {
                        uint16_t narrow;
                        if (atom->is_big_endian) {
                            narrow = ZL_readBE16(state->src + state->pos);
                        } else {
                            narrow = ZL_readLE16(state->src + state->pos);
                        }
                        if (atom->is_signed) {
                            return ZL_SDDL_Expr_makeNum(
                                    (ZL_SDDL_IntT)(int16_t)narrow);
                        } else {
                            return ZL_SDDL_Expr_makeNum((ZL_SDDL_IntT)narrow);
                        }
                    }
                    case 4: {
                        uint32_t narrow;
                        if (atom->is_big_endian) {
                            narrow = ZL_readBE32(state->src + state->pos);
                        } else {
                            narrow = ZL_readLE32(state->src + state->pos);
                        }
                        if (atom->is_signed) {
                            return ZL_SDDL_Expr_makeNum(
                                    (ZL_SDDL_IntT)(int32_t)narrow);
                        } else {
                            return ZL_SDDL_Expr_makeNum((ZL_SDDL_IntT)narrow);
                        }
                    }
                    case 8: {
                        if (atom->is_big_endian) {
                            return ZL_SDDL_Expr_makeNum(
                                    (ZL_SDDL_IntT)ZL_readBE64(
                                            state->src + state->pos));
                        } else {
                            return ZL_SDDL_Expr_makeNum(
                                    (ZL_SDDL_IntT)ZL_readLE64(
                                            state->src + state->pos));
                        }
                    }
                    default:
                        ZL_ASSERT_FAIL("Illegal width");
                        return ZL_SDDL_Expr_makeNull();
                }
            } else {
                return ZL_SDDL_Expr_makeNull();
            }
        case ZL_Type_struct:
            ZL_ASSERT_FAIL("Unsupported atom type.");
            return ZL_SDDL_Expr_makeNull();
        case ZL_Type_string:
            ZL_ASSERT_FAIL("Unsupported atom type.");
            return ZL_SDDL_Expr_makeNull();
        default:
            ZL_ASSERT_FAIL("Unknown atom type!");
            return ZL_SDDL_Expr_makeNull();
    }
}

static ZL_Report ZL_SDDL_State_applyCachedInstructions(
        ZL_SDDL_State* const state,
        const ZL_SDDL_CachedInstructions* const instrs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->opCtx);

    const size_t instr_count = instrs->count;
    const size_t total_size  = instrs->total_size;

    ZL_ERR_IF_GT(state->pos + total_size, state->size, srcSize_tooSmall);

    const size_t old_vec_size = VECTOR_SIZE(state->segment_tags);
    const size_t new_vec_size = old_vec_size + instr_count;

    ZL_ERR_IF_LT(
            VECTOR_RESIZE_UNINITIALIZED(state->segment_sizes, new_vec_size),
            new_vec_size,
            allocation);
    ZL_ERR_IF_LT(
            VECTOR_RESIZE_UNINITIALIZED(state->segment_tags, new_vec_size),
            new_vec_size,
            allocation);

    memcpy(&VECTOR_AT(state->segment_sizes, old_vec_size),
           instrs->lens,
           instr_count * sizeof(size_t));
    memcpy(&VECTOR_AT(state->segment_tags, old_vec_size),
           instrs->tags,
           instr_count * sizeof(uint32_t));

    state->pos += total_size;

    return ZL_returnSuccess();
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_consumeAtom(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Field_Atom* const atom)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);

    const size_t width = atom->width;
    const uint32_t tag = atom->dest.dest;

    ZL_ASSERT_LT(tag, state->num_tags);

    if (width == 0) {
        return ZL_WRAP_VALUE(ZL_SDDL_Expr_makeNull());
    }

    ZL_ERR_IF_GT(state->pos + width, state->size, srcSize_tooSmall);

    ZL_ERR_IF_ERR(ZL_SDDL_State_updateDest(state, tag, atom));

    const size_t vec_size = VECTOR_SIZE(state->segment_tags);

    if (ZL_UNLIKELY(vec_size == 0)
        || tag != VECTOR_AT(state->segment_tags, vec_size - 1)) {
        ZL_ERR_IF(!VECTOR_PUSHBACK(state->segment_sizes, width), allocation);
        ZL_ERR_IF(!VECTOR_PUSHBACK(state->segment_tags, tag), allocation);
    } else {
        // Add it onto the existing instruction for the same tag.
        VECTOR_AT(state->segment_sizes, vec_size - 1) += width;
    }

    const ZL_SDDL_Expr result = ZL_SDDL_State_readAtom(state, atom);

    state->pos += width;

    return ZL_WRAP_VALUE(result);
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_consumeArray_ofAtoms(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Field_Atom* const atom,
        const size_t arr_len)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);

    const size_t atom_width = atom->width;
    const uint32_t tag      = atom->dest.dest;

    // TODO: handle overflow?
    const size_t width = atom_width * arr_len;

    ZL_ASSERT_LT(tag, state->num_tags);

    if (width == 0) {
        return ZL_WRAP_VALUE(ZL_SDDL_Expr_makeNull());
    }

    ZL_ERR_IF_GT(state->pos + width, state->size, srcSize_tooSmall);

    ZL_ERR_IF_ERR(ZL_SDDL_State_updateDest(state, tag, atom));

    const size_t vec_size = VECTOR_SIZE(state->segment_tags);

    if (ZL_UNLIKELY(vec_size == 0)
        || tag != VECTOR_AT(state->segment_tags, vec_size - 1)) {
        ZL_ERR_IF(!VECTOR_PUSHBACK(state->segment_sizes, width), allocation);
        ZL_ERR_IF(!VECTOR_PUSHBACK(state->segment_tags, tag), allocation);
    } else {
        // Add it onto the existing instruction for the same tag.
        VECTOR_AT(state->segment_sizes, vec_size - 1) += width;
    }

    state->pos += width;

    return ZL_WRAP_VALUE(ZL_SDDL_Expr_makeNull());
}

static ZL_Report ZL_SDDL_State_consumeRecord_withScope(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Field_Record* const record,
        ZL_SDDL_Scope* const scope)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->opCtx);
    const ZL_SDDL_DynExprSet* const dyn = record->dyn;
    ZL_ERR_IF_NULL(dyn, GENERIC);
    for (size_t i = 0; i < dyn->num_exprs; i++) {
        const ZL_SDDL_Expr* const expr = &dyn->exprs[i];
        const ZL_SDDL_Var* const var   = dyn->names[i];
        ZL_ERR_IF_NE(expr->type, ZL_SDDL_ExprType_field, corruption);
        ZL_TRY_LET(
                ZL_SDDL_Expr,
                field_result,
                ZL_SDDL_State_consumeField(state, &expr->field));

        if (scope != NULL && var != NULL) {
            ZL_ERR_IF_ERR(ZL_SDDL_Scope_set(state, scope, var, &field_result));
        }
        ZL_SDDL_Expr_decref(state, &field_result);
    }
    return ZL_returnSuccess();
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_consumeRecord(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Field_Record* const record)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);
    ZL_SDDL_Scope* const scope = ZL_SDDL_Scope_create(state);
    ZL_ERR_IF_NULL(scope, allocation);

    const ZL_Report result =
            ZL_SDDL_State_consumeRecord_withScope(state, record, scope);

    if (ZL_RES_isError(result)) {
        ZL_SDDL_Scope_decref(state, scope);
    }
    ZL_ERR_IF_ERR(result);

    return ZL_WRAP_VALUE(ZL_SDDL_Expr_makeScope(scope));
}

static ZL_Report ZL_SDDL_State_consumeRecord_noScope(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Field_Record* const record,
        bool should_cache_instrs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(state->opCtx);

    if (record->dyn && record->dyn->instrs) {
        return ZL_SDDL_State_applyCachedInstructions(
                state, record->dyn->instrs);
    }

    if (!should_cache_instrs) {
        return ZL_SDDL_State_consumeRecord_withScope(state, record, NULL);
    }

    const size_t cur_pos = state->pos;

    const size_t cur_instr_size = VECTOR_SIZE(state->segment_tags);
    const size_t cur_instr_len  = cur_instr_size > 0
             ? VECTOR_AT(state->segment_sizes, cur_instr_size - 1)
             : 0;

    ZL_ERR_IF_ERR(ZL_SDDL_State_consumeRecord_withScope(state, record, NULL));

    const size_t new_instr_size = VECTOR_SIZE(state->segment_tags);
    size_t count                = new_instr_size - cur_instr_size;
    uint32_t* tag_start = &VECTOR_AT(state->segment_tags, cur_instr_size);
    size_t* len_start   = &VECTOR_AT(state->segment_sizes, cur_instr_size);

    const size_t total_size = state->pos - cur_pos;

    const size_t new_cur_instr_len  = cur_instr_size > 0
             ? VECTOR_AT(state->segment_sizes, cur_instr_size - 1)
             : 0;
    const size_t first_instr_offset = new_cur_instr_len - cur_instr_len;

    if (first_instr_offset) {
        tag_start--;
        len_start--;
        count++;
    }

    ZL_SDDL_CachedInstructions* const instrs =
            ZL_SDDL_CachedInstructions_create(
                    state,
                    tag_start,
                    len_start,
                    count,
                    total_size,
                    first_instr_offset);
    ZL_ERR_IF_NULL(instrs, allocation);

    record->dyn->instrs = instrs;

    return ZL_returnSuccess();
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_consumeArray(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Field_Array* const array)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);
    const ZL_SDDL_DynExprSet* const dyn = array->dyn;
    ZL_ERR_IF_NULL(dyn, GENERIC);
    const ZL_SDDL_Expr* const inner_expr = &dyn->exprs[0];
    const ZL_SDDL_Expr* const len_expr   = &dyn->exprs[1];
    ZL_ERR_IF(
            inner_expr->type != ZL_SDDL_ExprType_field
                    && inner_expr->type != ZL_SDDL_ExprType_func,
            corruption);
    ZL_ERR_IF_NE(len_expr->type, ZL_SDDL_ExprType_num, corruption);
    ZL_ERR_IF_LT(len_expr->num.val, 0, corruption);
    const size_t len = (size_t)len_expr->num.val;

    if (inner_expr->type == ZL_SDDL_ExprType_field) {
        if (inner_expr->field.type == ZL_SDDL_FieldType_atom) {
            // Optimization
            return ZL_SDDL_State_consumeArray_ofAtoms(
                    state, &inner_expr->field.atom, len);
        }

        if (inner_expr->field.type == ZL_SDDL_FieldType_record && len > 1) {
            // Optimization
            ZL_ERR_IF_ERR(ZL_SDDL_State_consumeRecord_noScope(
                    state, &inner_expr->field.record, true));

            const size_t instr_count =
                    inner_expr->field.record.dyn->instrs->count;

            // As an optimization, these reserves are allowed to fail.
            (void)VECTOR_RESERVE(
                    state->segment_sizes,
                    VECTOR_SIZE(state->segment_sizes)
                            + instr_count * (len - 1));
            (void)VECTOR_RESERVE(
                    state->segment_tags,
                    VECTOR_SIZE(state->segment_tags) + instr_count * (len - 1));

            for (size_t i = 1; i < len; i++) {
                ZL_ERR_IF_ERR(ZL_SDDL_State_consumeRecord_noScope(
                        state, &inner_expr->field.record, false));
            }
            return ZL_WRAP_VALUE(ZL_SDDL_Expr_makeNull());
        }
    }

    for (size_t i = 0; i < len; i++) {
        ZL_TRY_LET(
                ZL_SDDL_Expr,
                field_result,
                ZL_SDDL_State_consume(state, inner_expr));
        ZL_SDDL_Expr_decref(state, &field_result);
    }
    return ZL_WRAP_VALUE(ZL_SDDL_Expr_makeNull());
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_consumeField(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Field* const field)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);
    switch (field->type) {
        case ZL_SDDL_FieldType_poison: {
            ZL_ERR(corruption,
                   "Tried to consume poison field!%s%.*s",
                   (field->poison.msg.size ? ": " : ""),
                   field->poison.msg.size,
                   field->poison.msg.data);
        }
        case ZL_SDDL_FieldType_atom: {
            return ZL_SDDL_State_consumeAtom(state, &field->atom);
        }
        case ZL_SDDL_FieldType_record: {
            return ZL_SDDL_State_consumeRecord(state, &field->record);
        }
        case ZL_SDDL_FieldType_array: {
            return ZL_SDDL_State_consumeArray(state, &field->array);
        }
        default:
            ZL_ERR(corruption, "Unknown field type %d!", field->type);
    }
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_consumeFunc(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Func* const func)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);
    ZL_ERR_IF_NE(
            func->num_unbound_args,
            0,
            corruption,
            "Can't invoke/consume a function that hasn't received all its arguments!");

    ZL_SDDL_Scope* scope;
    if (func->scope != NULL
        && ZL_SDDL_RefCount_count(&func->scope->refs) == 1) {
        // If the func passed in is the sole reference holder to the scope, we
        // can exploit the fact that we are consuming that argument and it will
        // otherwise be thrown away: we can steal the scope and thereby avoid
        // having to copy it.
        scope = func->scope;
        ZL_SDDL_Scope_incref(state, scope);
    } else {
        scope = ZL_SDDL_Scope_createCopy(state, func->scope);
        ZL_ERR_IF_NULL(scope, allocation);
    }

    for (size_t i = 0; i < func->num_exprs; i++) {
        ZL_TRY_LET(
                ZL_SDDL_Expr,
                expr_result,
                ZL_SDDL_State_execExpr(state, scope, &func->exprs[i]));
        ZL_SDDL_Expr_decref(state, &expr_result);
    }

    return ZL_WRAP_VALUE(ZL_SDDL_Expr_makeScope(scope));
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_consume(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Expr* const expr)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);
    if (expr->type == ZL_SDDL_ExprType_field) {
        return ZL_SDDL_State_consumeField(state, &expr->field);
    } else if (expr->type == ZL_SDDL_ExprType_func) {
        return ZL_SDDL_State_consumeFunc(state, &expr->func);
    } else {
        ZL_ERR(corruption,
               "Can't consume an expression of type %s!",
               ZL_SDDL_ExprType_toString(expr->type));
    }
}

// Forward declaration
static ZL_RESULT_OF(ZL_SDDL_IntT) ZL_SDDL_State_sizeofField(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Expr* const expr);

static ZL_RESULT_OF(ZL_SDDL_IntT) ZL_SDDL_State_sizeofRecord(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Field_Record* const record)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_IntT, state->opCtx);
    const ZL_SDDL_DynExprSet* const dyn = record->dyn;
    ZL_ERR_IF_NULL(dyn, GENERIC);
    ZL_SDDL_IntT result = 0;
    for (size_t i = 0; i < dyn->num_exprs; i++) {
        const ZL_SDDL_Expr* const expr = &dyn->exprs[i];
        ZL_ERR_IF_NE(expr->type, ZL_SDDL_ExprType_field, corruption);
        ZL_TRY_LET_CONST(
                ZL_SDDL_IntT, size, ZL_SDDL_State_sizeofField(state, expr));
        // TODO: handle overflow
        result += size;
    }
    return ZL_WRAP_VALUE(result);
}

static ZL_RESULT_OF(ZL_SDDL_IntT) ZL_SDDL_State_sizeofArray(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Field_Array* const array)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_IntT, state->opCtx);
    const ZL_SDDL_DynExprSet* const dyn = array->dyn;
    ZL_ERR_IF_NULL(dyn, GENERIC);
    const ZL_SDDL_Expr* const inner_expr = &dyn->exprs[0];
    const ZL_SDDL_Expr* const len_expr   = &dyn->exprs[1];
    ZL_ERR_IF_NE(inner_expr->type, ZL_SDDL_ExprType_field, corruption);
    ZL_ERR_IF_NE(len_expr->type, ZL_SDDL_ExprType_num, corruption);
    ZL_ERR_IF_LT(len_expr->num.val, 0, corruption);
    ZL_TRY_LET_CONST(
            ZL_SDDL_IntT,
            elt_size,
            ZL_SDDL_State_sizeofField(state, inner_expr));
    // TODO: catch overflow
    const ZL_SDDL_IntT size = elt_size * len_expr->num.val;
    return ZL_WRAP_VALUE(size);
}

static ZL_RESULT_OF(ZL_SDDL_IntT) ZL_SDDL_State_sizeofField(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Expr* const expr)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_IntT, state->opCtx);
    ZL_ERR_IF_NE(expr->type, ZL_SDDL_ExprType_field, corruption);
    const ZL_SDDL_Field* const field = &expr->field;
    switch (field->type) {
        case ZL_SDDL_FieldType_poison: {
            return ZL_WRAP_VALUE(0);
        }
        case ZL_SDDL_FieldType_atom: {
            return ZL_WRAP_VALUE((ZL_SDDL_IntT)field->atom.width);
        }
        case ZL_SDDL_FieldType_record: {
            return ZL_SDDL_State_sizeofRecord(state, &field->record);
        }
        case ZL_SDDL_FieldType_array: {
            return ZL_SDDL_State_sizeofArray(state, &field->array);
        }
        default:
            ZL_ERR(corruption, "Unknown field type %d!", field->type);
    }
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_execExpr_send(
        ZL_SDDL_State* const state,
        const ZL_SDDL_Expr* const field,
        const ZL_SDDL_Expr* const dest)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);
    ZL_ERR_IF_NE(field->type, ZL_SDDL_ExprType_field, corruption);
    ZL_ERR_IF_NE(dest->type, ZL_SDDL_ExprType_dest, corruption);
    ZL_SDDL_Expr result = *field;
    ZL_ERR_IF_NE(
            result.field.type,
            ZL_SDDL_FieldType_atom,
            corruption,
            "Can't send non-atom field.");
    result.field.atom.dest = dest->dest;
    return ZL_WRAP_VALUE(result);
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_execExpr_bind(
        ZL_SDDL_State* const state,
        ZL_SDDL_Scope* const scope,
        const ZL_SDDL_Expr* const func,
        const ZL_SDDL_Expr* const args)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);
    ZL_ERR_IF_NE(func->type, ZL_SDDL_ExprType_func, corruption);
    ZL_ERR_IF_NE(args->type, ZL_SDDL_ExprType_tuple, corruption);

    // Note that other than because of this equality check, this would support
    // partial application no problem.
    ZL_ERR_IF_GT(
            args->tuple.num_exprs,
            func->func.num_unbound_args,
            corruption,
            "Function expected at most %zu arguments but got %zu.",
            func->func.num_unbound_args,
            args->tuple.num_exprs);

    ZL_SDDL_Expr result = *func;

    if (result.func.scope != NULL
        && ZL_SDDL_RefCount_count(&result.func.scope->refs) == 1) {
        // If the func passed in is the sole reference holder to the scope, we
        // can exploit the fact that we are consuming that argument and it will
        // otherwise be thrown away: we can steal the scope and thereby avoid
        // having to copy it.
        ZL_SDDL_Scope_incref(state, result.func.scope);
    } else {
        result.func.scope = ZL_SDDL_Scope_createCopy(state, func->func.scope);
        ZL_ERR_IF_NULL(result.func.scope, allocation);
    }

    for (size_t i = 0; i < args->tuple.num_exprs; i++) {
        ZL_TRY_LET(
                ZL_SDDL_Expr,
                val,
                ZL_SDDL_State_execExpr(state, scope, &args->tuple.exprs[i]));
        ZL_ERR_IF_ERR(ZL_SDDL_Scope_set(
                state, result.func.scope, &result.func.unbound_args[i], &val));
        ZL_SDDL_Expr_decref(state, &val);
    }

    result.func.unbound_args += args->tuple.num_exprs;
    result.func.num_unbound_args -= args->tuple.num_exprs;

    return ZL_WRAP_VALUE(result);
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_execExpr_op_inner(
        ZL_SDDL_State* const state,
        ZL_SDDL_Scope* const scope,
        const ZL_SDDL_Expr args[ZL_SDDL_OP_ARG_COUNT],
        const size_t num_args,
        const ZL_SDDL_Op* const op)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);

    (void)num_args;

    // ZL_LOG(ALWAYS, "Executing %s", ZL_SDDL_OpCode_toString(op->op));
    // for (size_t i = 0; i < num_args; i++) {
    //     log_expr(&args[i]);
    // }

    ZL_SDDL_Expr result = ZL_SDDL_Expr_makeNull();

    switch (op->op) {
        case ZL_SDDL_OpCode_die: {
            ZL_ERR(GENERIC, "Reached die op! Gaak.");
        }
        case ZL_SDDL_OpCode_expect: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_EQ(
                    args[0].num.val,
                    0,
                    corruption,
                    "Expect op got 0-valued argument. Failing the parse.");
            break;
        }
        case ZL_SDDL_OpCode_log: {
            // #ifndef NDEBUG

            log_expr(&args[0]);

            const ZL_RESULT_OF(ZL_SDDL_SourceLocationPrettyString) pstr_res =
                    ZL_SDDL_SourceLocationPrettyString_create(
                            ZL_ERR_CTX_PTR,
                            state->arena,
                            &state->prog->source_code,
                            &state->cur_src_loc,
                            2);

            ZL_ASSERT(
                    !ZL_RES_isError(pstr_res),
                    "Error in ZL_SDDL_SourceLocationPrettyString_create(): %s",
                    ZL_E_str(ZL_RES_error(pstr_res)));

            if (!ZL_RES_isError(pstr_res)) {
                const ZL_SDDL_SourceLocationPrettyString pstr =
                        ZL_RES_value(pstr_res);
                if (pstr.str.data != NULL) {
                    ZL_RLOG(ALWAYS, "at %.*s", pstr.str.size, pstr.str.data);
                }

                ZL_SDDL_SourceLocationPrettyString_destroy(state->arena, &pstr);
            }
            // #endif
            result = args[0];
            break;
        }
        case ZL_SDDL_OpCode_consume: {
            ZL_TRY_SET(
                    ZL_SDDL_Expr,
                    result,
                    ZL_SDDL_State_consume(state, &args[0]));
            break;
        }
        case ZL_SDDL_OpCode_sizeof: {
            ZL_TRY_LET_CONST(
                    ZL_SDDL_IntT,
                    val,
                    ZL_SDDL_State_sizeofField(state, &args[0]));
            result = ZL_SDDL_Expr_makeNum(val);
            break;
        }
        case ZL_SDDL_OpCode_send: {
            ZL_TRY_SET(
                    ZL_SDDL_Expr,
                    result,
                    ZL_SDDL_State_execExpr_send(state, &args[0], &args[1]));
            break;
        }
        case ZL_SDDL_OpCode_assign: {
            ZL_ERR_IF_NULL(
                    scope,
                    corruption,
                    "Can't assign to a variable here. (No scope to assign into!)");
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_var, corruption);

            ZL_TRY_SET(
                    ZL_SDDL_Expr,
                    result,
                    ZL_SDDL_Scope_set(state, scope, &args[0].var, &args[1]));
            ZL_SDDL_Expr_incref(state, &result);
            break;
        }
        case ZL_SDDL_OpCode_member: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_scope, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_var, corruption);

            ZL_TRY_SET(
                    ZL_SDDL_Expr,
                    result,
                    ZL_SDDL_Scope_get(state, args[0].scope, &args[1].var));
            break;
        }
        case ZL_SDDL_OpCode_bind: {
            ZL_TRY_SET(
                    ZL_SDDL_Expr,
                    result,
                    ZL_SDDL_State_execExpr_bind(
                            state, scope, &args[0], &args[1]));
            break;
        }
        case ZL_SDDL_OpCode_neg: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(-args[0].num.val);
            break;
        }
        case ZL_SDDL_OpCode_eq: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(args[0].num.val == args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_ne: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(args[0].num.val != args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_gt: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(args[0].num.val > args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_ge: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(args[0].num.val >= args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_lt: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(args[0].num.val < args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_le: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(args[0].num.val <= args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_add: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(args[0].num.val + args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_sub: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(args[0].num.val - args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_mul: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(args[0].num.val * args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_div: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_EQ(
                    args[1].num.val, 0, corruption, "Can't divide by zero.");
            result = ZL_SDDL_Expr_makeNum(args[0].num.val / args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_mod: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_EQ(
                    args[1].num.val, 0, corruption, "Modulus can't be zero.");
            result = ZL_SDDL_Expr_makeNum(args[0].num.val % args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_bit_and: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(args[0].num.val & args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_bit_or: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(args[0].num.val | args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_bit_xor: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(args[0].num.val ^ args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_bit_not: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(~args[0].num.val);
            break;
        }
        case ZL_SDDL_OpCode_log_and: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(args[0].num.val && args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_log_or: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            ZL_ERR_IF_NE(args[1].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(args[0].num.val || args[1].num.val);
            break;
        }
        case ZL_SDDL_OpCode_log_not: {
            ZL_ERR_IF_NE(args[0].type, ZL_SDDL_ExprType_num, corruption);
            result = ZL_SDDL_Expr_makeNum(!args[0].num.val);
            break;
        }
        default:
            ZL_ERR(corruption, "Unknown opcode %d", op->op);
    }

    // log_expr(&result);

    return ZL_WRAP_VALUE(result);
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_execExpr_op(
        ZL_SDDL_State* const state,
        ZL_SDDL_Scope* const scope,
        const ZL_SDDL_Op* const op)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);

    const size_t num_args = ZL_SDDL_OpCode_numArgs(op->op);
    ZL_SDDL_Expr args[ZL_SDDL_OP_ARG_COUNT];
    for (size_t i = 0; i < num_args; i++) {
        ZL_ERR_IF_NULL(op->args[i], corruption);
        if (op->op == ZL_SDDL_OpCode_assign && i == 0
            && op->args[i]->type == ZL_SDDL_ExprType_var) {
            args[i] = *op->args[i];
        } else if (
                op->op == ZL_SDDL_OpCode_member && i == 1
                && op->args[i]->type == ZL_SDDL_ExprType_var) {
            args[i] = *op->args[i];
        } else {
            ZL_TRY_SET(
                    ZL_SDDL_Expr,
                    args[i],
                    ZL_SDDL_State_execExpr(state, scope, op->args[i]));
        }
    }

    const ZL_RESULT_OF(ZL_SDDL_Expr) result =
            ZL_SDDL_State_execExpr_op_inner(state, scope, args, num_args, op);

    for (size_t i = 0; i < num_args; i++) {
        ZL_SDDL_Expr_decref(state, &args[i]);
    }

    return result;
}

static bool ZL_SDDL_State_execExpr_field_record_isExprAssume(
        const ZL_SDDL_Expr* const expr)
{
    if (expr->type != ZL_SDDL_ExprType_op) {
        return false;
    }
    const ZL_SDDL_Op* const op = &expr->op;
    if (op->op != ZL_SDDL_OpCode_assign) {
        return false;
    }
    const ZL_SDDL_Expr* const lhs = op->args[0];
    const ZL_SDDL_Expr* const rhs = op->args[1];
    if (lhs->type != ZL_SDDL_ExprType_var) {
        return false;
    }
    if (rhs->type != ZL_SDDL_ExprType_op) {
        return false;
    }
    const ZL_SDDL_Op* const rhsop = &rhs->op;
    if (rhsop->op != ZL_SDDL_OpCode_consume) {
        return false;
    }
    return true;
}

static bool ZL_SDDL_State_execExpr_field_record_isExprConsume(
        const ZL_SDDL_Expr* const expr)
{
    if (expr->type != ZL_SDDL_ExprType_op) {
        return false;
    }
    const ZL_SDDL_Op* const op = &expr->op;
    if (op->op != ZL_SDDL_OpCode_consume) {
        return false;
    }
    return true;
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_execExpr_field(
        ZL_SDDL_State* const state,
        ZL_SDDL_Scope* const scope,
        const ZL_SDDL_Field* const field)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);
    // TODO
    // ZL_SDDL_DynExprSet_create(state, 1);
    ZL_SDDL_Expr result;
    result.type  = ZL_SDDL_ExprType_field;
    result.field = *field;
    result.loc   = (ZL_SDDL_SourceLocation){
          .start = 0,
          .size  = 0,
    };
    switch (result.field.type) {
        case ZL_SDDL_FieldType_poison:
            break;
        case ZL_SDDL_FieldType_atom: {
            if (result.field.atom.width_expr != NULL) {
                ZL_TRY_LET_CONST(
                        ZL_SDDL_Expr,
                        width,
                        ZL_SDDL_State_execExpr(
                                state, scope, result.field.atom.width_expr));
                ZL_ERR_IF_NE(width.type, ZL_SDDL_ExprType_num, corruption);
                ZL_ERR_IF_LT(width.num.val, 0, corruption);
                result.field.atom.width      = (size_t)width.num.val;
                result.field.atom.width_expr = NULL;
            }
            break;
        }
        case ZL_SDDL_FieldType_record: {
            ZL_SDDL_Field_Record* const record = &result.field.record;
            if (record->dyn != NULL) {
                // Already resolved.
                ZL_SDDL_DynExprSet_incref(state, record->dyn);
                break;
            }
            record->dyn =
                    ZL_SDDL_DynExprSet_create(state, record->num_exprs, true);
            ZL_ERR_IF_NULL(record->dyn, allocation);
            for (size_t i = 0; i < record->num_exprs; i++) {
                const ZL_SDDL_Expr* const expr = record->exprs[i];
                if (ZL_SDDL_State_execExpr_field_record_isExprAssume(expr)) {
                    const ZL_SDDL_Var* const var = &expr->op.args[0]->var;
                    const ZL_SDDL_Expr* const field_expr =
                            expr->op.args[1]->op.args[0];
                    record->dyn->names[i] = var;
                    ZL_TRY_SET(
                            ZL_SDDL_Expr,
                            record->dyn->exprs[i],
                            ZL_SDDL_State_execExpr(state, scope, field_expr));
                } else if (ZL_SDDL_State_execExpr_field_record_isExprConsume(
                                   expr)) {
                    // Skip over consume op. Record members are implicitly
                    // consumed.
                    const ZL_SDDL_Expr* const field_expr = expr->op.args[0];
                    ZL_TRY_SET(
                            ZL_SDDL_Expr,
                            record->dyn->exprs[i],
                            ZL_SDDL_State_execExpr(state, scope, field_expr));
                } else {
                    ZL_TRY_SET(
                            ZL_SDDL_Expr,
                            record->dyn->exprs[i],
                            ZL_SDDL_State_execExpr(state, scope, expr));
                }
            }
            break;
        }
        case ZL_SDDL_FieldType_array: {
            ZL_SDDL_Field_Array* const array = &result.field.array;
            if (array->dyn != NULL) {
                // Already resolved.
                ZL_SDDL_DynExprSet_incref(state, array->dyn);
                break;
            }
            array->dyn = ZL_SDDL_DynExprSet_create(state, 2, false);
            ZL_ERR_IF_NULL(array->dyn, allocation);
            ZL_TRY_SET(
                    ZL_SDDL_Expr,
                    array->dyn->exprs[0],
                    ZL_SDDL_State_execExpr(state, scope, array->expr));
            ZL_TRY_SET(
                    ZL_SDDL_Expr,
                    array->dyn->exprs[1],
                    ZL_SDDL_State_execExpr(state, scope, array->len));
            break;
        }
    }
    return ZL_WRAP_VALUE(result);
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_execExpr_var(
        ZL_SDDL_State* const state,
        ZL_SDDL_Scope* const scope,
        const ZL_SDDL_Var* const var)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);

    if (StringView_eqCStr(&var->name, "_rem")) {
        return ZL_WRAP_VALUE(ZL_SDDL_Expr_makeNum(
                (ZL_SDDL_IntT)state->size - (ZL_SDDL_IntT)state->pos));
    }
    if (StringView_eqCStr(&var->name, "_pos")) {
        return ZL_WRAP_VALUE(ZL_SDDL_Expr_makeNum((ZL_SDDL_IntT)state->pos));
    }

    return ZL_SDDL_Scope_get(state, scope, var);
}

static ZL_RESULT_OF(ZL_SDDL_Expr) ZL_SDDL_State_execExpr(
        ZL_SDDL_State* const state,
        ZL_SDDL_Scope* const scope,
        const ZL_SDDL_Expr* const expr)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Expr, state->opCtx);
    ZL_ERR_IF_NULL(expr, parameter_invalid);

    const ZL_SDDL_SourceLocation old_src_loc = state->cur_src_loc;
    if (expr->loc.start != 0 || expr->loc.size != 0) {
        state->cur_src_loc = expr->loc;
    }

    ZL_SDDL_Expr result;
    switch (expr->type) {
        case ZL_SDDL_ExprType_null:
            result = *expr;
            break;
        case ZL_SDDL_ExprType_op:
            ZL_TRY_SET(
                    ZL_SDDL_Expr,
                    result,
                    ZL_SDDL_State_execExpr_op(state, scope, &expr->op));
            break;
        case ZL_SDDL_ExprType_num:
            result = *expr;
            break;
        case ZL_SDDL_ExprType_field:
            ZL_TRY_SET(
                    ZL_SDDL_Expr,
                    result,
                    ZL_SDDL_State_execExpr_field(state, scope, &expr->field));
            break;
        case ZL_SDDL_ExprType_dest:
            result = *expr;
            break;
        case ZL_SDDL_ExprType_var:
            ZL_TRY_SET(
                    ZL_SDDL_Expr,
                    result,
                    ZL_SDDL_State_execExpr_var(state, scope, &expr->var));
            break;
        case ZL_SDDL_ExprType_scope:
            result = *expr;
            break;
        case ZL_SDDL_ExprType_tuple:
            // Sub-items are evaluated as the tuple is unpacked... surely
            // nothing unexpected could happen as a result of that...
            result = *expr;
            break;
        case ZL_SDDL_ExprType_func:
            result = *expr;
            break;
        default:
            ZL_ERR(logicError, "Unknown expression type %d!", expr->type);
    }

    state->cur_src_loc = old_src_loc;

    return ZL_WRAP_VALUE(result);
}

ZL_RESULT_OF(ZL_SDDL_Instructions)
ZL_SDDL_State_exec(
        ZL_SDDL_State* const state,
        const void* const src,
        const size_t srcSize)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Instructions, NULL);
    ZL_ERR_IF_NULL(state, parameter_invalid);
    ZL_RESULT_UPDATE_SCOPE_CONTEXT(state->opCtx);

    ZL_ERR_IF_NULL(src, parameter_invalid);

    state->src  = (const uint8_t*)src;
    state->size = srcSize;
    state->pos  = 0;

    for (size_t i = 0; i < state->prog->num_root_exprs; i++) {
        ZL_RESULT_OF(ZL_SDDL_Expr)
        result = ZL_SDDL_State_execExpr(
                state, state->globals, &state->prog->root_exprs[i]);

        if (ZL_RES_isError(result)) {
            const ZL_RESULT_OF(ZL_SDDL_SourceLocationPrettyString) pstr_res =
                    ZL_SDDL_SourceLocationPrettyString_create(
                            ZL_ERR_CTX_PTR,
                            state->arena,
                            &state->prog->source_code,
                            &state->cur_src_loc,
                            2);

            ZL_ASSERT(
                    !ZL_RES_isError(pstr_res),
                    "Error in ZL_SDDL_SourceLocationPrettyString_create(): %s",
                    ZL_E_str(ZL_RES_error(pstr_res)));

            if (!ZL_RES_isError(pstr_res)) {
                const ZL_SDDL_SourceLocationPrettyString pstr =
                        ZL_RES_value(pstr_res);
                if (pstr.str.data != NULL) {
                    ZL_E_ADDFRAME(
                            &ZL_RES_error(result),
                            ZL_EE_EMPTY,
                            "\nEncountered error at position %zu while processing:\n%.*s",
                            state->pos,
                            pstr.str.size,
                            pstr.str.data);
                }

                ZL_SDDL_SourceLocationPrettyString_destroy(state->arena, &pstr);
            }

            ZL_ERR_IF_ERR(result);
        }
        ZL_SDDL_Expr expr = ZL_RES_value(result);
        // log_expr(&expr);
        ZL_SDDL_Expr_decref(state, &expr);
    }

    ZL_ERR_IF_NE(
            state->pos,
            state->size,
            srcSize_tooLarge,
            "Data description did not consume the whole input.");

    ZL_SDDL_Scope_clear(state, state->globals);
    // There should be exactly one scope living, the globals, and it should be
    // empty.
    ZL_ERR_IF_NE(
            state->num_scopes_creates,
            state->num_scopes_destroys + 1,
            GENERIC,
            "Incorrectly tracked scope lifetimes!");

    ZL_ERR_IF_NE(
            state->num_dyn_expr_sets_creates,
            state->num_dyn_expr_sets_destroys,
            GENERIC,
            "Incorrectly tracked expression lifetimes!");

    ZL_SDDL_Instructions instructions = { 0 };

    instructions.dispatch_instructions.nbSegments =
            VECTOR_SIZE(state->segment_sizes);
    ZL_ERR_IF_GT(
            VECTOR_SIZE(state->dests), UINT_MAX, nodeExecution_invalidOutputs);
    instructions.dispatch_instructions.nbTags =
            (uint32_t)VECTOR_SIZE(state->dests);

    instructions.dispatch_instructions.segmentSizes =
            VECTOR_DATA(state->segment_sizes);
    instructions.dispatch_instructions.tags = VECTOR_DATA(state->segment_tags);

    instructions.outputs    = VECTOR_DATA(state->dests);
    instructions.numOutputs = VECTOR_SIZE(state->dests);

    return ZL_WRAP_VALUE(instructions);
}

const char* ZL_SDDL_State_getErrorContextString_fromError(
        const ZL_SDDL_State* const state,
        ZL_Error error)
{
    if (state == NULL) {
        return NULL;
    }
    if (!ZL_E_isError(error)) {
        return NULL;
    }
    return ZL_OC_getErrorContextString(state->opCtx, error);
}

/***********
 * Binding *
 ***********/

typedef struct {
    ZL_SDDL_Program* prog;
    ZL_SDDL_State* state;
} ParseObjects;

static void ParseObjects_destroy(const ParseObjects* pos)
{
    ZL_SDDL_State_free(pos->state);
    ZL_SDDL_Program_free(pos->prog);
}

static ZL_RESULT_OF(ZL_SDDL_Instructions) ZL_SDDL_dynGraph_exec(
        ZL_Graph* const gctx,
        ParseObjects* const pos,
        const ZL_Input* const in,
        const StringView program)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_SDDL_Instructions, gctx);

    ZL_OperationContext* const opCtx = ZL_Graph_getOperationContext(gctx);

    pos->prog = ZL_SDDL_Program_create(opCtx);
    ZL_ERR_IF_NULL(pos->prog, allocation);

    ZL_ERR_IF_ERR(ZL_SDDL_Program_load(pos->prog, program.data, program.size));

    pos->state = ZL_SDDL_State_create(pos->prog, opCtx);
    ZL_ERR_IF_NULL(pos->state, allocation);

    return ZL_SDDL_State_exec(
            pos->state, ZL_Input_ptr(in), ZL_Input_contentSize(in));
}

static ZL_Report ZL_SDDL_dynGraph_inner(
        ZL_Graph* const gctx,
        ParseObjects* const pos,
        ZL_Edge** const inputs,
        const size_t nbInputs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(gctx);
    ZL_ERR_IF_NE(nbInputs, 1, graph_invalidNumInputs);
    ZL_Edge* const inEdge       = inputs[0];
    const ZL_Input* const input = ZL_Edge_getData(inEdge);
    ZL_ERR_IF_NE(
            ZL_Input_type(input), ZL_Type_serial, node_unexpected_input_type);

    const ZL_NodeIDList custom_nodes = ZL_Graph_getCustomNodes(gctx);
    ZL_ERR_IF_NE(custom_nodes.nbNodeIDs, 0, graphParameter_invalid);
    const ZL_GraphIDList successor_graphs = ZL_Graph_getCustomGraphs(gctx);
    ZL_ERR_IF_NE(successor_graphs.nbGraphIDs, 1, graphParameter_invalid);
    const ZL_GraphID successor_graph = successor_graphs.graphids[0];

    const ZL_RefParam param =
            ZL_Graph_getLocalRefParam(gctx, ZL_SDDL_DESCRIPTION_PID);
    ZL_ERR_IF_NE(
            param.paramId, ZL_SDDL_DESCRIPTION_PID, graphParameter_invalid);
    ZL_ERR_IF_NULL(param.paramRef, graphParameter_invalid);
    ZL_ERR_IF_EQ(param.paramSize, 0, graphParameter_invalid);
    const StringView program = StringView_init(param.paramRef, param.paramSize);

    ZL_TRY_LET_CONST(
            ZL_SDDL_Instructions,
            instructions,
            ZL_SDDL_dynGraph_exec(gctx, pos, input, program));

    ZL_TRY_LET(
            ZL_EdgeList,
            edges,
            ZL_Edge_runDispatchNode(
                    inEdge, &instructions.dispatch_instructions));

    ZL_ERR_IF_NE(
            edges.nbEdges,
            instructions.numOutputs + 2,
            nodeExecution_invalidOutputs);

    ZL_EdgeList converted_edges;
    converted_edges.edges = (ZL_Edge**)ZL_Graph_getScratchSpace(
            gctx, edges.nbEdges * sizeof(ZL_Edge*));
    converted_edges.nbEdges = edges.nbEdges;
    ZL_ERR_IF_NULL(converted_edges.edges, allocation);

    for (size_t i = 0; i < edges.nbEdges; i++) {
        converted_edges.edges[i] = edges.edges[i];
    }

    // Don't convert the first two streams.
    for (size_t i = 2; i < edges.nbEdges; i++) {
        ZL_Edge* const edge                = edges.edges[i];
        const ZL_SDDL_OutputInfo* const oi = &instructions.outputs[i - 2];
        if (oi->width == 0) {
            continue;
            // Never set up.
        }

        switch (oi->type) {
            case ZL_Type_serial:
                // Do nothing.
                break;
            case ZL_Type_numeric: {
                ZL_NodeID conversion_nid;
                switch (oi->width) {
                    case 1:
                    case 2:
                    case 4:
                    case 8:
                        if (oi->big_endian) {
                            conversion_nid =
                                    ZL_Node_convertSerialToNumBE(oi->width * 8);
                        } else {
                            conversion_nid =
                                    ZL_Node_convertSerialToNumLE(oi->width * 8);
                        }
                        break;
                    default:
                        ZL_ERR(node_unexpected_input_type,
                               "Unhandled output stream width (%llu) from dispatch.",
                               (long long unsigned)oi->width);
                }
                ZL_TRY_LET_CONST(
                        ZL_EdgeList,
                        new_edges,
                        ZL_Edge_runNode(edge, conversion_nid));
                ZL_ERR_IF_NE(new_edges.nbEdges, 1, successor_invalidNumInputs);
                converted_edges.edges[i] = new_edges.edges[0];
                break;
            }
            case ZL_Type_struct:
            case ZL_Type_string:
                ZL_ERR(node_unexpected_input_type,
                       "Unhandled output stream type from dispatch. SDDL should only produce serial and numeric streams.");
        }
    }

    for (size_t i = 0; i < converted_edges.nbEdges; i++) {
        ZL_ERR_IF_ERR(
                ZL_Edge_setIntMetadata(converted_edges.edges[i], 0, (int)i));
    }

    ZL_ERR_IF_ERR(ZL_Edge_setParameterizedDestination(
            converted_edges.edges,
            converted_edges.nbEdges,
            successor_graph,
            NULL));

    return ZL_returnSuccess();
}

ZL_Report ZL_SDDL_dynGraph(
        ZL_Graph* const gctx,
        ZL_Edge** const inputs,
        const size_t nbInputs) ZL_NOEXCEPT_FUNC_PTR
{
    ParseObjects pos = {
        .prog  = NULL,
        .state = NULL,
    };
    const ZL_Report result =
            ZL_SDDL_dynGraph_inner(gctx, &pos, inputs, nbInputs);
    ParseObjects_destroy(&pos);
    return result;
}

ZL_RESULT_OF(ZL_GraphID)
ZL_Compressor_buildSDDLGraph(
        ZL_Compressor* const compressor,
        const void* const program,
        const size_t programSize,
        const ZL_GraphID successor)
{
    ZL_RESULT_DECLARE_SCOPE(ZL_GraphID, compressor);

    const ZL_CopyParam cp = {
        .paramId   = ZL_SDDL_DESCRIPTION_PID,
        .paramPtr  = program,
        .paramSize = programSize,
    };
    const ZL_LocalParams lp = {
        .intParams = {0},
        .copyParams = {
            .copyParams = &cp,
            .nbCopyParams = 1,
        },
        .refParams = {0},
    };
    const ZL_ParameterizedGraphDesc desc = {
        .name           = NULL,
        .graph          = ZL_GRAPH_SDDL,
        .customGraphs   = &successor,
        .nbCustomGraphs = 1,
        .customNodes    = NULL,
        .nbCustomNodes  = 0,
        .localParams    = &lp,
    };

    const ZL_GraphID gid =
            ZL_Compressor_registerParameterizedGraph(compressor, &desc);
    ZL_ERR_IF(!ZL_GraphID_isValid(gid), graph_invalid);
    return ZL_WRAP_VALUE(gid);
}
