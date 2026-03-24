// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_PARSERS_PARQUET_LEXER_H
#define CUSTOM_PARSERS_PARQUET_LEXER_H

#include "openzl/shared/portability.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

typedef struct ZL_ParquetLexer_s ZL_ParquetLexer;

/**
 * Creates a new Parquet lexer.
 */
ZL_ParquetLexer* ZL_ParquetLexer_create(void);

/**
 * Frees a Parquet lexer.
 */
void ZL_ParquetLexer_free(ZL_ParquetLexer* lexer);

/**
 * Initializes a Parquet lexer on the given input buffer.
 *
 * @returns Success if the input buffer may be a valid Parquet file,
 *          or an error code if the input file is definitely not
 *          a supported Parquet file.
 */
ZL_Report ZL_ParquetLexer_init(
        ZL_ParquetLexer* lexer,
        const void* src,
        size_t srcSize,
        ZL_ErrorContext* opctx);

/**
 * Returns true if the lexer has reached the end of the input buffer.
 */
bool ZL_ParquetLexer_finished(const ZL_ParquetLexer* lexer);

/**
 * The type of token in a parquet file.
 */
typedef enum {
    ZL_ParquetTokenType_Magic,
    ZL_ParquetTokenType_Footer,
    ZL_ParquetTokenType_PageHeader,
    ZL_ParquetTokenType_DataPage,
} ZL_ParquetTokenType;

typedef struct {
    /// Pointer to the beginning of the token in the source buffer.
    const char* ptr;
    /// Size of the token in bytes.
    size_t size;
    /// Type of the token.
    ZL_ParquetTokenType type;

    /// The following fields are only valid for DataPage tokens.

    /// The tag associated with the data page. All column chunks with the same
    /// schema path should have the same tag.
    uint32_t tag;
    /// The type and width of the elements in the data page.
    ZL_Type dataType;
    size_t dataWidth;
} ZL_ParquetToken;

/**
 * Lexes the next @p outCapacity tokens from the input buffer.
 *
 * @returns The number of tokens lexed, or an error code.
 *          Upon success, the input may be a valid Parquet file,
 *          and upon error the input is definitely not a supported
 *          Parquet file. Once it returns a value < @p outCapacity,
 *          the input has been fully lexed, and it will return 0
 *          on subsequent calls.
 */
ZL_Report ZL_ParquetLexer_lex(
        ZL_ParquetLexer* lexer,
        ZL_ParquetToken* out,
        size_t outCapacity,
        ZL_ErrorContext* opctx);

/**
 * @returns The maximum number of tokens that can be lexed from the input if it
 * is a valid Parquet file.
 * @note Will return an error if the lexer has not been successfully
 * initialized.
 */
ZL_Report ZL_ParquetLexer_maxNumTokens(
        ZL_ParquetLexer* lexer,
        ZL_ErrorContext* opctx);

ZL_END_C_DECLS
#endif
