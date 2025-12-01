// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_GRAPHS_SIMPLE_DATA_DESCRIPTION_LANGUAGE_SOURCE_CODE_H
#define OPENZL_GRAPHS_SIMPLE_DATA_DESCRIPTION_LANGUAGE_SOURCE_CODE_H

#include "openzl/zl_errors.h"

#include "openzl/shared/portability.h"
#include "openzl/shared/string_view.h"

#include "openzl/common/allocation.h"

ZL_BEGIN_C_DECLS

/**
 * @file
 *
 * This file declares some utilities related to processing references to the
 * source code from which an SDDL program was compiled. Primarily that means
 * pretty-printing the source code corresponding to an expression when an
 * error is encountered during execution of that expression.
 */

/***************
 * Source Code *
 ***************/

typedef struct {
    StringView source_code;
} ZL_SDDL_SourceCode;

void ZL_SDDL_SourceCode_init(
        Arena* arena,
        ZL_SDDL_SourceCode* sc,
        StringView sv);

void ZL_SDDL_SourceCode_initEmpty(Arena* arena, ZL_SDDL_SourceCode* sc);

void ZL_SDDL_SourceCode_destroy(Arena* arena, ZL_SDDL_SourceCode* sc);

/*******************
 * Source Location *
 *******************/

typedef struct {
    size_t start;
    size_t size;
} ZL_SDDL_SourceLocation;

/***********************************
 * Source Location Pretty-Printing *
 ***********************************/

typedef struct {
    StringView str;

    /// Non-const pointer to the backing buffer so it can be freed. Ignore it.
    char* _buf;
} ZL_SDDL_SourceLocationPrettyString;

ZL_RESULT_DECLARE_TYPE(ZL_SDDL_SourceLocationPrettyString);

ZL_RESULT_OF(ZL_SDDL_SourceLocationPrettyString)
ZL_SDDL_SourceLocationPrettyString_create(
        ZL_ErrorContext* ctx,
        Arena* arena,
        const ZL_SDDL_SourceCode* sc,
        const ZL_SDDL_SourceLocation* sl,
        size_t indent);

void ZL_SDDL_SourceLocationPrettyString_destroy(
        Arena* arena,
        const ZL_SDDL_SourceLocationPrettyString* pretty_str);

ZL_END_C_DECLS

#endif // OPENZL_GRAPHS_SIMPLE_DATA_DESCRIPTION_LANGUAGE_SOURCE_CODE_H
