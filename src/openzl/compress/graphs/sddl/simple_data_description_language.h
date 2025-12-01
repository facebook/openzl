// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_GRAPHS_SIMPLE_DATA_DESCRIPTION_LANGUAGE_H
#define OPENZL_GRAPHS_SIMPLE_DATA_DESCRIPTION_LANGUAGE_H

#include "openzl/codecs/zl_sddl.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_graph_api.h"

#include "openzl/shared/portability.h"
#include "openzl/shared/string_view.h"

#include "openzl/common/allocation.h"
#include "openzl/common/opaque_types_internal.h"

ZL_BEGIN_C_DECLS

/**
 * Simple Data Description Language Code Specification:
 *
 * This module defines a domain-specific programming language which allows a
 * program author to describe how to parse an input. This module consumes
 * binary, "compiled" representations of these programs. A separate tool, the
 * SDDL Compiler, in `tools/sddl/compiler/`,
 * consumes syntactically pleasant representations of programs and translate
 * them into the intermediate, "compiled" representation on which this module
 * operates. This document defines only the schema and semantics of that
 * compiled representation. See the documentation for SDDL for a description of
 * the human-writable language that the compiler accepts.
 *
 * The SDDL (Simple Data Description Language) binary language is fundamentally
 * composed of Expressions. A program is a CBOR-serialized bytestream, whose
 * deserialized value is primarily a list of Expressions. (The actual structure
 * of a serialized program is a map which has an "exprs" element which is the
 * list of compiled expressions. It optionally also has a "src" element, which
 * is the source code the program was compiled from, which is used in error
 * messages to aid debuggability.)
 *
 * Expressions are represented as a map. That map always has one or two
 * elements. The first key/value pair is the description of the expression:
 * the key is a string that describes the kind of the Expression and the value
 * provides any additional information needed by that kind of Expression, as
 * described below. The second key/value pair is optional, and contains debug
 * context. The key, if present, is "dbg", and the value is a map. Currently,
 * that map has one defined entry which has the key "loc" and the value is a
 * pair of integers which represent respectively the start index and length
 * of the portion of the source code (captured in the top-level "src" item)
 * to which this expression corresponds.
 *
 * For the key/value pair that represents the expression, the valid keys are
 * as follows, and they map to the following overall types of Expressions:
 *
 * | Key     | Expr Type |
 * +---------+-----------+
 * | die     | Op        |
 * | expect  | Op        |
 * | log     | Op        |
 * | consume | Op        |
 * | sizeof  | Op        |
 * | send    | Op        |
 * | assign  | Op        |
 * | member  | Op        |
 * | bind    | Op        |
 * | eq      | Op        |
 * | ne      | Op        |
 * | gt      | Op        |
 * | ge      | Op        |
 * | lt      | Op        |
 * | le      | Op        |
 * | neg     | Op        |
 * | add     | Op        |
 * | sub     | Op        |
 * | mul     | Op        |
 * | div     | Op        |
 * | mod     | Op        |
 * | bit_and | Op        |
 * | bit_or  | Op        |
 * | bit_xor | Op        |
 * | bit_not | Op        |
 * | log_and | Op        |
 * | log_or  | Op        |
 * | log_not | Op        |
 * | int     | Num       |
 * | poison  | Field     |
 * | atom    | Field     |
 * | record  | Field     |
 * | array   | Field     |
 * | var     | Var       |
 * | tuple   | Tuple     |
 * | func    | Func      |
 *
 * ## Semantics for Different Expression Types
 *
 * ### Op
 *
 * An Op describes an operation to perform on its zero or more arguments. The
 * value of an Op expression is an array. In the case that the particular Op in
 * question takes no arguments, the value may also be `null`. The following
 * table describes the behaviors of the available operations.
 *
 * Type abbreviations: N = null, O = op, I = num, F = field, D = dest, V = var
 *
 * | Op      | Args |Result|Arg#1| Arg#2 | Effect
 * +---------+------+------+-----+-------+--------
 * | die     | 0    | N    |     |       | Unconditionally fail
 * | expect  | 1    | N    | IV  |       | Fail the parse if arg is 0
 * | log     | 1    | *    | *   |       | Logs the arg to stderr for debug
 * | consume | 1    | INS  | FV  |       | Consumes a field, see below
 * | sizeof  | 1    | I    | FV  |       | (Recursize) size of given field
 * | send    | 2    | F    | FV  | DV    | New field assoc'ed w/ dest
 * | assign  | 2    | OIFD | V   | OIFDV | lhs = eval(rhs)
 * | member  | 2    | Any  | S   | V     | Looks up rhs in the lhs namespace.
 * | bind    | 2    | Func | Func| Tuple | Applies args to func.
 * | eq      | 2    | I    | IV  | IV    | eval(lhs) == eval(rhs)
 * | ne      | 2    | I    | IV  | IV    | eval(lhs) != eval(rhs)
 * | gt      | 2    | I    | IV  | IV    | eval(lhs) >  eval(rhs)
 * | ge      | 2    | I    | IV  | IV    | eval(lhs) >= eval(rhs)
 * | lt      | 2    | I    | IV  | IV    | eval(lhs) <  eval(rhs)
 * | le      | 2    | I    | IV  | IV    | eval(lhs) <= eval(rhs)
 * | neg     | 1    | I    | IV  |       | - eval(arg)
 * | add     | 2    | I    | IV  | IV    | eval(lhs) + eval(rhs)
 * | sub     | 2    | I    | IV  | IV    | eval(lhs) - eval(rhs)
 * | mul     | 2    | I    | IV  | IV    | eval(lhs) * eval(rhs)
 * | div     | 2    | I    | IV  | IV    | eval(lhs) / eval(rhs)
 * | mod     | 2    | I    | IV  | IV    | eval(lhs) % eval(rhs)
 * | bit_and | 2    | I    | IV  | IV    | eval(lhs) & eval(rhs)
 * | bit_or  | 2    | I    | IV  | IV    | eval(lhs) | eval(rhs)
 * | bit_xor | 2    | I    | IV  | IV    | eval(lhs) ^ eval(rhs)
 * | bit_not | 1    | I    | IV  |       | ~eval(arg)
 * | log_and | 2    | I    | IV  | IV    | eval(lhs) && eval(rhs)
 * | log_or  | 2    | I    | IV  | IV    | eval(lhs) || eval(rhs)
 * | log_not | 1    | I    | IV  |       | !eval(arg)
 *
 * ### Num
 *
 * A Num expression is a literal numeric value. The valid range of values is
 * those representable by an `int64_t`. The value of the pair in the map
 * representation of a Num expression is that integer value.
 *
 * ### Field
 *
 * A field represents a single or compound collection of elementary types,
 * which can be consumed.
 *
 * There are currently four kinds of Fields:
 *
 * - Poison: causes the parse to fail if consumed. The value can be null or a
 *   string which is included in the error message bubbled up. (Note: not yet
 *   implemented.)
 *
 * - Atom: a single, indivisible field of predefined type. The map value is one
 *   of the following strings, and the atom takes on that key's corresponding
 *   properties listed in the following table:
 *
 *   | Name    | ZL_Type | Size | Signed | Endianness | Returns Val? |
 *   +---------+---------+------+--------+------------+--------------+
 *   | byte    | Serial  | 1    | No     | N/A        | Yes          |
 *   | i1      | Numeric | 1    | Yes    | N/A        | Yes          |
 *   | u1      | Numeric | 1    | No     | N/A        | Yes          |
 *   | i2l     | Numeric | 2    | Yes    | Little     | Yes          |
 *   | i2b     | Numeric | 2    | Yes    | Big        | Yes          |
 *   | u2l     | Numeric | 2    | No     | Little     | Yes          |
 *   | u2b     | Numeric | 2    | No     | Big        | Yes          |
 *   | i4l     | Numeric | 4    | Yes    | Little     | Yes          |
 *   | i4b     | Numeric | 4    | Yes    | Big        | Yes          |
 *   | u4l     | Numeric | 4    | No     | Little     | Yes          |
 *   | u4b     | Numeric | 4    | No     | Big        | Yes          |
 *   | i8l     | Numeric | 8    | Yes    | Little     | Yes          |
 *   | i8b     | Numeric | 8    | Yes    | Big        | Yes          |
 *   | u8l     | Numeric | 8    | No     | Little     | Yes          |
 *   | u8b     | Numeric | 8    | No     | Big        | Yes          |
 *   | f1      | Numeric | 1    | Yes    | N/A        | No           |
 *   | f2l     | Numeric | 2    | Yes    | Little     | No           |
 *   | f2b     | Numeric | 2    | Yes    | Big        | No           |
 *   | f4l     | Numeric | 4    | Yes    | Little     | No           |
 *   | f4b     | Numeric | 4    | Yes    | Big        | No           |
 *   | f8l     | Numeric | 8    | Yes    | Little     | No           |
 *   | f8b     | Numeric | 8    | Yes    | Big        | No           |
 *   | bf1     | Numeric | 1    | Yes    | N/A        | No           |
 *   | bf2l    | Numeric | 2    | Yes    | Little     | No           |
 *   | bf2b    | Numeric | 2    | Yes    | Big        | No           |
 *   | bf4l    | Numeric | 4    | Yes    | Little     | No           |
 *   | bf4b    | Numeric | 4    | Yes    | Big        | No           |
 *   | bf8l    | Numeric | 8    | Yes    | Little     | No           |
 *   | bf8b    | Numeric | 8    | Yes    | Big        | No           |
 *
 * - Record: a struct-like compound type. The map value is a list of
 *   expressions each of which must resolve to a field at record evaluation
 *   time, and which represents the ordered list of fields which this record
 *   contains.
 *
 * - Array: an array-like compound type. The map value is a pair of expressions,
 *   the first of which must resolve to a field which is the inner field and
 *   the second of which must resolve to a num, which is the length of the
 *   array.
 *
 * ### Dest:
 *
 * TBD. In flux.
 *
 * ### Var:
 *
 * A reference by name to a storage slot for an expression. The map value of a
 * var expression is a string which is the name of the variable.
 *
 * Variables are global (currently). When a var expression is evaluated in any
 * context other than as the left-hand argument to the assignment operator, it
 * resolves to the expression that was most recently assigned into that var.
 *
 * There are some built-in variables which can be read but which can't be
 * assigned to:
 *
 * | Variable | Type | Evaluates To           |
 * +----------+------+------------------------+
 * | `_pos`   | Int  | Bytes consumed so far. |
 * | `_rem`   | Int  | Bytes remaining.       |
 *
 * ### Tuple:
 *
 * A Tuple expression is just a list of expressions, used by the bind op to
 * apply args to a function. The map value is an array whose elements are the
 * representations of the expressions.
 *
 * ### Func:
 *
 * A Func expression declares a function that can later be bound to args and
 * then invoked. Its map value is an array with two elements. The first element
 * is an array of strings, which are the names of its parameters. The second
 * element is an array of expressions, which are the expressions that make up
 * its body.
 *
 * ## To-Do:
 *
 * - Deterministic compound fields can have a materialized tag/size vec that
 *   can just be memcpy()-ed in.
 */

/************
 * Programs *
 ************/

typedef struct ZL_SDDL_Program_s ZL_SDDL_Program;

/**
 * Create a program object.
 *
 * Currently, a `ZL_SDDL_Program` may only be loaded into once. Once loaded,
 * it may be executed (via @ref ZL_SDDL_State_create() and then @ref
 * ZL_SDDL_State_exec()) any number of times.
 *
 * @p opCtx may be NULL, in which case a local context is created.
 */
ZL_SDDL_Program* ZL_SDDL_Program_create(ZL_OperationContext* opCtx);

void ZL_SDDL_Program_free(ZL_SDDL_Program* prog);

ZL_Report
ZL_SDDL_Program_load(ZL_SDDL_Program* prog, const void* src, size_t srcSize);

/**
 * Safely retrieve the full error message associated with an error.
 *
 * ```
 * ZL_RESULT_OF(Something) result = ZL_SDDL_Program_doSomething(prog, ...);
 * if (ZL_RES_isError(result)) {
 *   const char* msg = ZL_SDDL_Program_getErrorContextString_fromError(
 *       prog, ZL_RES_error(result));
 * }
 * ```
 *
 * @returns the verbose error message associated with the @p error or `NULL`
 *          if the error is no longer valid.
 *
 * @note This string is stored within the @p prog and is only valid for the
 *       lifetime of the @p prog.
 */
const char* ZL_SDDL_Program_getErrorContextString_fromError(
        const ZL_SDDL_Program* prog,
        ZL_Error error);

/*******************
 * Execution State *
 *******************/

typedef struct ZL_SDDL_State_s ZL_SDDL_State;

typedef struct {
    ZL_Type type; // 0 when unused.
    size_t width; // 0 when unused.
    bool big_endian;
} ZL_SDDL_OutputInfo;

/**
 * Captures the resulting dispatch instructions produced by running an SDDL
 * program over an input.
 *
 * The memory backing these arrays is owned by the ZL_SDDL_State and will be
 * freed when the state is freed or reset.
 *
 * @note that the tags in these instructions, as well as the output info array
 * do not correspond to the outputs you actually get if you invoke @ref
 * ZL_Edge_runDispatchNode with these instructions, because the dispatch
 * codec emits two streams (the tags and segment sizes) before all of the
 * streams content has been dispatched into.
 */
typedef struct {
    ZL_DispatchInstructions dispatch_instructions;

    const ZL_SDDL_OutputInfo* outputs;
    size_t numOutputs;
} ZL_SDDL_Instructions;

/**
 * Create an SDDL execution state object.
 *
 * Currently, a `ZL_SDDL_State` object may only be used for a single execution,
 * after which it should be freed and recreated, if needed.
 *
 * @p opCtx may be NULL, in which case a local context is created.
 */
ZL_SDDL_State* ZL_SDDL_State_create(
        const ZL_SDDL_Program* prog,
        ZL_OperationContext* opCtx);

void ZL_SDDL_State_free(ZL_SDDL_State* state);

ZL_RESULT_DECLARE_TYPE(ZL_SDDL_Instructions);

/**
 * Applies the program referenced in @p state during @ref ZL_SDDL_State_create
 * to the input @p src, and returns the produced instructions.
 */
ZL_RESULT_OF(ZL_SDDL_Instructions)
ZL_SDDL_State_exec(ZL_SDDL_State* state, const void* src, size_t srcSize);

/**
 * Safely retrieve the full error message associated with an error.
 *
 * ```
 * ZL_RESULT_OF(Something) result = ZL_SDDL_State_doSomething(state, ...);
 * if (ZL_RES_isError(result)) {
 *   const char* msg = ZL_SDDL_State_getErrorContextString_fromError(
 *       state, ZL_RES_error(result));
 * }
 * ```
 *
 * @returns the verbose error message associated with the @p error or `NULL`
 *          if the error is no longer valid.
 *
 * @note This string is stored within the @p state and is only valid for the
 *       lifetime of the @p state.
 */
const char* ZL_SDDL_State_getErrorContextString_fromError(
        const ZL_SDDL_State* state,
        ZL_Error error);

/************
 * Dispatch *
 ************/

/**
 * Graph function that is the basis for the SDDL standard graph.
 *
 * Expects to receive the compiled description to execute at param ID @ref
 * ZL_SDDL_DESCRIPTION_PID.
 */
ZL_Report ZL_SDDL_dynGraph(ZL_Graph* gctx, ZL_Edge* inputs[], size_t nbInputs)
        ZL_NOEXCEPT_FUNC_PTR;

ZL_END_C_DECLS

#endif // OPENZL_GRAPHS_SIMPLE_DATA_DESCRIPTION_LANGUAGE_H
