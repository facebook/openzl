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
    // Inputs
    size_t nbColumns;
    uint32_t* stringLens;
    uint16_t* dispatchIndices;
    size_t* newlineIndices;
    // Outputs
    size_t nbNewlines;
    size_t nbStrs;
} ZL_CSV_lexResult;

ZL_Report ZL_CSV_lex(
        ZL_Segmenter* sctx,
        const char* const content,
        size_t byteSize,
        bool hasHeader,
        char sep,
        ZL_CSV_lexResult* retLexResult);

// Instead of doing a full columnar dispatch, we skip the dispatch if the column
// is empty and coalesce the separators together. So we have a result with
// uneven columns, depending on how many empty values are in each column.
ZL_Report ZL_CSV_lexNullAware(
        ZL_Segmenter* sctx,
        const char* const content,
        size_t byteSize,
        bool hasHeader,
        char sep,
        ZL_CSV_lexResult* retLexResult);

ZL_Report createParsedCsv(
        ZL_CSV_lexResult* retLexResult,
        const char* content,
        const size_t length,
        char sep);

ZL_Report createNullAwareLexAndDispatch(
        ZL_CSV_lexResult* retLexResult,
        const char* content,
        const size_t length,
        char sep);

typedef enum {
    ZL_CSV_TokenType_Field,
    ZL_CSV_TokenType_Sep,
    ZL_CSV_TokenType_Newline,
} ZL_CSV_TokenType;

typedef struct {
    ZL_CSV_TokenType type;
    uint32_t size;
    uint32_t col;
} ZL_CSV_Token;

typedef struct {
    ZL_OperationContext* opCtx;
    const char* start;
    const char* src;
    const char* end;
    char sep;
    bool isNullAware;
    /// If row == 0, then 0, else number of columns
    uint32_t numCols;
    /// Current column
    uint32_t col;
    /// Current row
    size_t row;
    bool isFieldNext;
} ZL_CSV_Lexer;

/**
 * Initializes a CSV lexer.
 *
 * @param src The source to lex (streaming not yet supported).
 * @param srcSize The size of the source.
 * @param sep The separator character. Typically ',', '|' or '\t'.
 * @param isNullAware Whether to use the null-aware parser, which coalesces
 *        consecutive separators into a single token.
 */
void ZL_CSV_Lexer_init(
        ZL_CSV_Lexer* lexer,
        ZL_OperationContext* opCtx,
        const char* src,
        size_t srcSize,
        char sep,
        bool isNullAware);

/**
 * Lexes tokens from the source.
 *
 * @param lexer The lexer state.
 * @param types The array of types to fill of size @p maxNumTokens.
 * @param sizes The array of sizes to fill of size @p maxNumTokens.
 * @param cols The array of cols to fill of size @p maxNumTokens. Set to 0 for
 *             non-field tokens.
 * @param maxNumTokens The maximum number of tokens to lex.
 * @param srcSizeLimit Once this limit is reached, the lexer will stop lexing
 *        after completing the CSV row.
 *
 * @returns The number of tokens lexed, or an error.
 */
ZL_Report ZL_CSV_Lexer_lex(
        ZL_CSV_Lexer* lexer,
        ZL_CSV_TokenType* types,
        uint32_t* sizes,
        uint32_t* cols,
        size_t maxNumTokens,
        size_t srcSizeLimit);

/**
 * @returns Whether the lexer has reached the end of the source.
 */
bool ZL_CSV_Lexer_finished(const ZL_CSV_Lexer* lexer);

#if defined(__cplusplus)
} // extern "C"
#endif
#endif // ZL_CUSTOM_PARSERS_CSV_CSV_LEXER_H
