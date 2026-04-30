// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/csv/csv_lexer.h"

#include <stdint.h>

#include "openzl/shared/utils.h"
#include "openzl/zl_errors.h"

void ZL_CSV_Lexer_init(
        ZL_CSV_Lexer* lexer,
        ZL_OperationContext* opCtx,
        const char* src,
        size_t srcSize,
        char sep,
        bool isNullAware)
{
    lexer->opCtx       = opCtx;
    lexer->start       = src;
    lexer->src         = src;
    lexer->end         = src + srcSize;
    lexer->isNullAware = isNullAware;
    lexer->sep         = sep;

    lexer->numCols     = 0;
    lexer->col         = 0;
    lexer->row         = 0;
    lexer->isFieldNext = true;
}

ZL_Report ZL_CSV_Lexer_lex(
        ZL_CSV_Lexer* lexer,
        ZL_CSV_TokenType* types,
        uint32_t* sizes,
        uint32_t* cols,
        size_t maxNumTokens,
        size_t srcSizeLimit)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(lexer->opCtx);

    const char* const limit = lexer->src
            + ZL_MIN((size_t)(lexer->end - lexer->src), srcSizeLimit);
    size_t out = 0;

    // Local lexer constants
    const char sep         = lexer->sep;
    const bool isNullAware = lexer->isNullAware;
    const char* const end  = lexer->end;

    // Local lexer state
    // Keep in local variables for two reasons:
    // 1. When an error occurs, guarantee the state is unmodified
    // 2. Make the compilers job easier
    const char* src  = lexer->src;
    size_t row       = lexer->row;
    uint32_t col     = lexer->col;
    uint32_t numCols = lexer->numCols;

    if (!lexer->isFieldNext || isNullAware) {
        // If we expect a separator or newline next, skip the field parsing
        // Null aware parsing always goes here, because if it is a field it will
        // just go back to the top of the loop.
        goto _lexSeparator;
    }

    while (src < end && out < maxNumTokens) {
        // Lex field
        // This always creates a token, even if the field is empty.
        {
            size_t size = 0;
            do {
                const char c = src[size];
                if (c == '"') {
                    // Handle quotes
                    do {
                        ++size;
                    } while (src + size < end && src[size] != '"');
                    ZL_ERR_IF_GE(
                            src + size,
                            end,
                            node_invalid_input,
                            "CSV file is not well formed: Unterminated quoted string");
                    ++size;
                } else if (c == '\n' || c == sep) {
                    break;
                } else {
                    ++size;
                }
            } while (src + size < end);

            types[out] = ZL_CSV_TokenType_Field;
            sizes[out] = (uint32_t)size;
            cols[out]  = col;

            src += size;
            ++out;
        }

    _lexSeparator:
        // If present, lex a separator or newline
        if (src < end && out < maxNumTokens) {
            const char c = *src;
            if (c == '\n') {
                if (row == 0) {
                    numCols = col + 1;
                }
                // Check that the number of columns is consistent
                ZL_ERR_IF_NE(
                        col + 1,
                        numCols,
                        node_invalid_input,
                        "CSV file is not well formed: Uneven number of columns");

                types[out] = ZL_CSV_TokenType_Newline;
                sizes[out] = 1;
                cols[out]  = 0;

                col = 0;
                ++row;
                ++src;
                ++out;

                if (src >= limit) {
                    // Stop on the first newline at or after the limit
                    break;
                }

                if (isNullAware) {
                    // If the first field is null we need to skip it rather than
                    // create an empty token. If it is non-null, this will just
                    // push us back to the top of the loop.
                    goto _lexSeparator;
                }
            } else if (c == sep) {
                types[out] = ZL_CSV_TokenType_Sep;
                cols[out]  = 0;

                size_t size = 1;
                if (isNullAware) {
                    // Handle multiple null fields
                    while (src + size < end && src[size] == sep) {
                        ++size;
                    }
                }
                sizes[out] = (uint32_t)size;
                col += (uint32_t)size;
                src += size;
                ++out;
            }
        }
    }
    ZL_ASSERT_LE(src, end);
    ZL_ASSERT_LE(out, maxNumTokens);

    // Update lexer state
    lexer->src     = src;
    lexer->row     = row;
    lexer->col     = col;
    lexer->numCols = numCols;
    if (out > 0) {
        lexer->isFieldNext = (types[out - 1] != ZL_CSV_TokenType_Field);
    }

    return ZL_returnValue(out);
}

bool ZL_CSV_Lexer_finished(const ZL_CSV_Lexer* lexer)
{
    return lexer->src == lexer->end;
}
