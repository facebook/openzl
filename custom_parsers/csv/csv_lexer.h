// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZL_CUSTOM_PARSERS_CSV_CSV_LEXER_H
#define ZL_CUSTOM_PARSERS_CSV_CSV_LEXER_H

#include <stdbool.h>
#include <stdint.h>

#include "openzl/zl_errors.h"
#include "openzl/zl_opaque_types.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
    size_t nbStrs;
    size_t nbColumns;
    uint32_t* stringLens;
    uint16_t* dispatchIndices;
} ZL_CSV_lexResult;

ZL_Report ZL_CSV_lex(
        ZL_Graph* gctx,
        const char* const content,
        size_t byteSize,
        bool hasHeader,
        char sep,
        ZL_CSV_lexResult* retLexResult);

// Instead of doing a full columnar dispatch, we skip the dispatch if the column
// is empty and coalesce the separators together. So we have a result with
// uneven columns, depending on how many empty values are in each column.
ZL_Report ZL_CSV_lexNullAware(
        ZL_Graph* gctx,
        const char* const content,
        size_t byteSize,
        bool hasHeader,
        char sep,
        ZL_CSV_lexResult* retLexResult);

ZL_Report createParsedCsv(
        uint32_t* stringLens,
        const char* content,
        const size_t length,
        char sep,
        size_t nbColumns);

ZL_Report createNullAwareLexAndDispatch(
        uint32_t* stringLens,
        uint16_t* dispatchIndices,
        const char* content,
        const size_t length,
        uint8_t nbColumns,
        char sep);

#if defined(__cplusplus)
} // extern "C"
#endif
#endif // ZL_CUSTOM_PARSERS_CSV_CSV_LEXER_H
