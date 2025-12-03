// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/csv/csv_lexer.h"

#include <stdint.h>

#include "openzl/codecs/zl_dispatch.h"
#include "openzl/common/logging.h"
#include "openzl/shared/utils.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_segmenter.h"

// Parses the CSV file to get the number of columns, separated by @p sep, and
// the length of the first row, including the ending `\n`.
static ZL_Report parseFirstRow(
        const char* content,
        const size_t length,
        char sep,
        size_t* nbColumns,
        size_t* firstRowLen)
{
    *nbColumns = 0;
    for (size_t i = 0; i < length; i++) {
        if (content[i] == '"') {
            do {
                i++;
            } while (i < length && content[i] != '"');
            if (i >= length) {
                ZL_RET_R_ERR(
                        node_invalid_input,
                        "CSV file is not well formed. Open quote is not closed");
            }
            i++;
            if (i >= length) {
                ZL_RET_R_ERR(
                        node_invalid_input,
                        "CSV file is not well formed. No newline character found anywhere in the file");
            }
        }
        if (content[i] == '\n') {
            *firstRowLen = i + 1;
            (*nbColumns)++;
            ZL_RET_R_IF_GT(
                    node_invalid_input,
                    *nbColumns,
                    ZL_DispatchString_maxDispatches() - 2,
                    "CSV file has more columns than supported by dispatchString");
            return ZL_returnSuccess();
        }
        if (content[i] == sep) {
            (*nbColumns)++;
        }
    }
    ZL_RET_R_ERR(
            node_invalid_input,
            "CSV file not well formed. No newline character found anywhere in the file");
}

static ZL_Report countNbNewlines(const char* content, const size_t length)
{
    size_t nbNewlines = 0;
    for (uint32_t i = 0; i < length; i++) {
        nbNewlines += (content[i] == '\n');
    }
    ZL_RET_R_IF(
            node_invalid_input,
            nbNewlines == 0 && length > 0,
            "No newline character found in a non-empty CSV body");
    return ZL_returnValue(nbNewlines);
}

/**
 * Creates dispatch indices for each string.
 * Given N columns, there are N + 2 dispatches:
 *   - Columns 0 through N - 1 to to dispatches 0 through N - 1
 *   - Delimiters, whitespace, and newlines to dispatch N
 *   - Header to dispatch N + 1
 */
// TODO: refactor this and the parsing fn to return an error if the assumption
// of equal-sized rows is violated
static ZL_Report createCsvDispatchIndices(
        uint16_t* dispatchIndices,
        size_t nbContentRows,
        size_t nbColumns,
        const uint32_t* stringLens)
{
    (void)stringLens;
    size_t maxDispatches = ZL_DispatchString_maxDispatches();
    ZL_RET_R_IF_GT(
            temporaryLibraryLimitation,
            nbColumns,
            maxDispatches - 2,
            "Dispatch only supports up to %i dispatches - 2 aux outputs = %i columns",
            maxDispatches,
            maxDispatches - 2);
    // We separate strings to follow the pattern of 'header',
    // 'content', 'separator', 'content', ..., therefore even indices are
    // separators.
    if (nbContentRows != 0) {
        size_t columnNumber = 0;
        for (size_t i = 0; i < 2 * nbColumns; i += 2) {
            dispatchIndices[i]     = (uint16_t)nbColumns;
            dispatchIndices[i + 1] = (uint16_t)columnNumber++;
        }
        dispatchIndices[2 * nbColumns] = (uint16_t)nbColumns;
        // memcpy in a loop! memcpy in a loop! memcpy in a loop!
        // (this can probably be faster)
        for (size_t row = 1; row < nbContentRows; ++row) {
            memcpy(dispatchIndices + 1 + row * (2 * nbColumns),
                   dispatchIndices + 1,
                   2 * nbColumns * sizeof(dispatchIndices[0]));
        }
    }
    // Header goes to a separate cluster
    dispatchIndices[0] = (uint16_t)(nbColumns + 1);
    return ZL_returnSuccess();
}

ZL_Report createParsedCsv(
        ZL_CSV_lexResult* retLexResult,
        const char* content,
        const size_t length,
        char sep)
{
    size_t nbColumns    = retLexResult->nbColumns;
    uint32_t fieldStart = 0;
    size_t nbStrs       = 1; // Header has been added
    size_t nbRows       = 0;
    size_t col          = 1;

    for (uint32_t i = 0; i < length; ++i) {
        // skip past all quoted strings
        while (content[i] == '"') {
            do {
                ++i;
            } while (i < length && content[i] != '"');
            if (i >= length) {
                ZL_RET_R_ERR(
                        node_invalid_input,
                        "CSV file is not well formed. Open quote is not closed");
            }
            ++i;
            if (i >= length) {
                ZL_RET_R_ERR(
                        node_invalid_input,
                        "CSV file is not well formed. No newline character at the end of the last line");
            }
        }
        if (content[i] == sep || content[i] == '\n') {
            // check for unexpected or missing columns
            if (content[i] == sep) {
                if (col >= nbColumns) {
                    ZL_RET_R_ERR(
                            node_invalid_input,
                            "CSV file is not well formed. Header expects %i columns, but found %i (or more) columns",
                            nbColumns,
                            col);
                }
                ++col;
            } else { // content[i] == '\n'
                if (col != nbColumns) {
                    ZL_RET_R_ERR(
                            node_invalid_input,
                            "CSV file is not well formed. Header expects %i columns, but only found %i columns",
                            nbColumns,
                            col);
                }
                col                                    = 1;
                retLexResult->newlineIndices[nbRows++] = nbStrs + 1;
            }

            retLexResult->stringLens[nbStrs] = i - fieldStart;
            ++nbStrs;
            retLexResult->stringLens[nbStrs] = 1;
            ++nbStrs;
            fieldStart = i + 1;
        }
    }
    ZL_RET_R_IF_NE(
            node_invalid_input,
            col,
            1,
            "CSV file may be truncated. Header expects %i columns, but only found %i columns in the last line",
            nbColumns,
            col - 1);
    ZL_RET_R_IF_NE(
            node_invalid_input,
            fieldStart,
            length,
            "CSV file not well formed. No newline character at the end of the last line");
    retLexResult->nbStrs     = nbStrs;
    retLexResult->nbNewlines = nbRows;
    ZL_LOG(V, "createParsedCsv nbStrs: %zu", nbStrs);
    return ZL_returnValue(nbStrs);
}

// returns number of strings processed
ZL_Report createNullAwareLexAndDispatch(
        ZL_CSV_lexResult* retLexResult,
        const char* content,
        const size_t length,
        char sep)
{
    uint32_t fieldStart = 0;
    uint8_t colIdx      = 0;
    size_t nbStrs       = 1; // Header has been added
    size_t nbRows       = 0;

    for (uint32_t i = 0; i < length;) {
        // skip past all quoted strings
        while (content[i] == '"') {
            do {
                ++i;
            } while (i < length && content[i] != '"');
            if (i >= length) {
                ZL_RET_R_ERR(
                        node_invalid_input,
                        "CSV file is not well formed. Open quote is not closed");
            }
            ++i;
        }
        if (content[i] == sep) {
            retLexResult->stringLens[nbStrs]      = i - fieldStart;
            retLexResult->dispatchIndices[nbStrs] = colIdx;
            ++nbStrs;
            fieldStart = i;
            // coalesce all contiguous separators, e.g. ',,,,,,'
            while (i < length && content[i] == sep) {
                ++colIdx;
                ++i;
            }
            retLexResult->stringLens[nbStrs] = i - fieldStart;
            retLexResult->dispatchIndices[nbStrs] =
                    (uint16_t)retLexResult->nbColumns;
            ++nbStrs;
            fieldStart = i;
            continue;
        }
        if (content[i] == '\n') {
            retLexResult->stringLens[nbStrs]      = i - fieldStart;
            retLexResult->dispatchIndices[nbStrs] = colIdx;
            ++nbStrs;
            retLexResult->stringLens[nbStrs] = 1;
            retLexResult->dispatchIndices[nbStrs] =
                    (uint16_t)retLexResult->nbColumns;
            retLexResult->newlineIndices[nbRows++] = nbStrs;
            ++nbStrs;
            fieldStart = i + 1;
            colIdx     = 0;
        }
        ++i;
    }
    retLexResult->nbStrs     = nbStrs;
    retLexResult->nbNewlines = nbRows;
    ZL_RET_R_IF_NE(
            node_invalid_input,
            fieldStart,
            length,
            "CSV file not well formed. No newline character at the end of the last line");
    ZL_LOG(V, "createParsedCsv nbStrs: %zu", nbStrs);
    return ZL_returnValue(nbStrs);
}

ZL_Report ZL_CSV_lex(
        ZL_Segmenter* sctx,
        const char* const content,
        size_t byteSize,
        bool hasHeader,
        char sep,
        ZL_CSV_lexResult* retLexResult)
{
    // TODO: can we use a vector to avoid this double iteration? would it be
    // faster?
    // pre-processing for rows, columns before parsing
    const char* rowsStart;
    size_t rowsByteSize;
    size_t nbColumns;
    {
        size_t firstRowLen =
                0; // dummy initialization, to avoid -Wmaybe-uninitialized
        ZL_RET_R_IF_ERR(parseFirstRow(
                content, byteSize, sep, &nbColumns, &firstRowLen));
        if (hasHeader) {
            rowsStart    = content + firstRowLen;
            rowsByteSize = byteSize - firstRowLen;
        } else {
            rowsStart    = content;
            rowsByteSize = byteSize;
        }
    }
    const ZL_Report maxNbRowsReport = countNbNewlines(rowsStart, rowsByteSize);
    ZL_RET_R_IF_ERR(maxNbRowsReport);
    const size_t maxNbRows = ZL_validResult(maxNbRowsReport);

    // Given 'n' columns, there are 'n' content strings and 'n' separator
    // strings per row. This is because we count the newline separator as well
    // as all the column separators. We add 1 for the header. Overcounting
    // extraneous quoted newlines is possible.
    const size_t maxNbStrings = 2 * nbColumns * maxNbRows + 1;

    uint32_t* stringLens =
            ZL_Segmenter_getScratchSpace(sctx, maxNbStrings * sizeof(uint32_t));
    size_t* newlineIndices =
            ZL_Segmenter_getScratchSpace(sctx, maxNbRows * sizeof(size_t));
    ZL_RET_R_IF_NULL(allocation, stringLens);
    stringLens[0] = (uint32_t)(rowsStart - content); // 0 if there is no header
    retLexResult->stringLens     = stringLens;
    retLexResult->nbColumns      = nbColumns;
    retLexResult->newlineIndices = newlineIndices;

    ZL_RET_R_IF_ERR(
            createParsedCsv(retLexResult, rowsStart, rowsByteSize, sep));
    size_t actualNbRows = retLexResult->nbStrs / (2 * nbColumns);

    uint16_t* dispatchIndices = ZL_Segmenter_getScratchSpace(
            sctx, retLexResult->nbStrs * sizeof(uint16_t));
    ZL_RET_R_IF_NULL(allocation, dispatchIndices);
    ZL_RET_R_IF_ERR(createCsvDispatchIndices(
            dispatchIndices, actualNbRows, nbColumns, stringLens));

    retLexResult->dispatchIndices = dispatchIndices;
    return ZL_returnSuccess();
}

ZL_Report ZL_CSV_lexNullAware(
        ZL_Segmenter* sctx,
        const char* const content,
        size_t byteSize,
        bool hasHeader,
        char sep,
        ZL_CSV_lexResult* retLexResult)
{
    // TODO: can we use a vector to avoid this double iteration? would it be
    // faster?
    // pre-processing for rows, columns before parsing
    const char* rowsStart;
    size_t rowsByteSize;
    size_t nbColumns;
    {
        size_t firstRowLen =
                0; // dummy initialization, to avoid -Wmaybe-uninitialized
        ZL_RET_R_IF_ERR(parseFirstRow(
                content, byteSize, sep, &nbColumns, &firstRowLen));
        if (hasHeader) {
            rowsStart    = content + firstRowLen;
            rowsByteSize = byteSize - firstRowLen;
        } else {
            rowsStart    = content;
            rowsByteSize = byteSize;
        }
    }
    const ZL_Report maxNbRowsReport = countNbNewlines(rowsStart, rowsByteSize);
    ZL_RET_R_IF_ERR(maxNbRowsReport);
    const size_t maxNbRows = ZL_validResult(maxNbRowsReport);

    // Given 'n' columns, there are up to 'n' content strings and 'n' separator
    // strings per row. This is because we count the newline separator as well
    // as all the column separators. We add 1 for the header. Overcounting
    // extraneous quoted newlines is possible.
    const size_t maxNbStrings = 2 * nbColumns * maxNbRows + 1;

    size_t* newlineIndices =
            ZL_Segmenter_getScratchSpace(sctx, maxNbRows * sizeof(size_t));
    uint32_t* stringLens =
            ZL_Segmenter_getScratchSpace(sctx, maxNbStrings * sizeof(uint32_t));
    ZL_RET_R_IF_NULL(allocation, stringLens);
    uint16_t* dispatchIndices =
            ZL_Segmenter_getScratchSpace(sctx, maxNbStrings * sizeof(uint16_t));
    ZL_RET_R_IF_NULL(allocation, dispatchIndices);
    stringLens[0] = (uint32_t)(rowsStart - content); // 0 if there is no header
    dispatchIndices[0]            = (uint16_t)nbColumns + 1; // header
    retLexResult->stringLens      = stringLens;
    retLexResult->dispatchIndices = dispatchIndices;
    retLexResult->nbColumns       = nbColumns;
    retLexResult->newlineIndices  = newlineIndices;
    ZL_RET_R_IF_ERR(createNullAwareLexAndDispatch(
            retLexResult, rowsStart, rowsByteSize, sep));
    return ZL_returnSuccess();
}

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
