// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/csv/csv_lexer.h"

#include <stdint.h>

#include "openzl/codecs/zl_dispatch.h"
#include "openzl/common/logging.h"
#include "openzl/zl_graph_api.h"

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
        uint32_t* stringLens,
        const char* content,
        const size_t length,
        char sep,
        size_t nbColumns)
{
    uint32_t fieldStart = 0;
    size_t nbStrs       = 0;
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
                col = 1;
            }

            stringLens[nbStrs] = i - fieldStart;
            ++nbStrs;
            stringLens[nbStrs] = 1;
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
    ZL_LOG(V, "createParsedCsv nbStrs: %zu", nbStrs);
    return ZL_returnValue(nbStrs);
}

// returns number of strings processed
ZL_Report createNullAwareLexAndDispatch(
        uint32_t* stringLens,
        uint16_t* dispatchIndices,
        const char* content,
        const size_t length,
        uint8_t nbColumns,
        char sep)
{
    uint32_t fieldStart = 0;
    uint8_t colIdx      = 0;
    size_t nbStrs       = 0;

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
            stringLens[nbStrs]      = i - fieldStart;
            dispatchIndices[nbStrs] = colIdx;
            ++nbStrs;
            fieldStart = i;
            // coalesce all contiguous separators, e.g. ',,,,,,'
            while (i < length && content[i] == sep) {
                ++colIdx;
                ++i;
            }
            stringLens[nbStrs]      = i - fieldStart;
            dispatchIndices[nbStrs] = nbColumns;
            ++nbStrs;
            fieldStart = i;
            continue;
        }
        if (content[i] == '\n') {
            stringLens[nbStrs]      = i - fieldStart;
            dispatchIndices[nbStrs] = colIdx;
            ++nbStrs;
            stringLens[nbStrs]      = 1;
            dispatchIndices[nbStrs] = nbColumns;
            ++nbStrs;
            fieldStart = i + 1;
            colIdx     = 0;
        }
        ++i;
    }
    ZL_RET_R_IF_NE(
            node_invalid_input,
            fieldStart,
            length,
            "CSV file not well formed. No newline character at the end of the last line");
    ZL_LOG(V, "createParsedCsv nbStrs: %zu", nbStrs);
    return ZL_returnValue(nbStrs);
}

ZL_Report ZL_CSV_lex(
        ZL_Graph* gctx,
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
            ZL_Graph_getScratchSpace(gctx, maxNbStrings * sizeof(uint32_t));
    ZL_RET_R_IF_NULL(allocation, stringLens);
    stringLens[0] = (uint32_t)(rowsStart - content); // 0 if there is no header
    ZL_Report rep = createParsedCsv(
            stringLens + 1, rowsStart, rowsByteSize, sep, nbColumns);
    ZL_RET_R_IF_ERR(rep);
    size_t actualNbStrs = ZL_validResult(rep);
    size_t actualNbRows = actualNbStrs / (2 * nbColumns);
    actualNbStrs += 1; // +1 for header

    uint16_t* dispatchIndices =
            ZL_Graph_getScratchSpace(gctx, actualNbStrs * sizeof(uint16_t));
    ZL_RET_R_IF_NULL(allocation, dispatchIndices);
    ZL_RET_R_IF_ERR(createCsvDispatchIndices(
            dispatchIndices, actualNbRows, nbColumns, stringLens));

    // return
    retLexResult->stringLens      = stringLens;
    retLexResult->dispatchIndices = dispatchIndices;
    retLexResult->nbStrs          = actualNbStrs;
    retLexResult->nbColumns       = nbColumns;

    return ZL_returnSuccess();
}

ZL_Report ZL_CSV_lexNullAware(
        ZL_Graph* gctx,
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

    uint32_t* stringLens =
            ZL_Graph_getScratchSpace(gctx, maxNbStrings * sizeof(uint32_t));
    ZL_RET_R_IF_NULL(allocation, stringLens);
    uint16_t* dispatchIndices =
            ZL_Graph_getScratchSpace(gctx, maxNbStrings * sizeof(uint16_t));
    ZL_RET_R_IF_NULL(allocation, dispatchIndices);
    stringLens[0] = (uint32_t)(rowsStart - content); // 0 if there is no header

    ZL_Report rep = createNullAwareLexAndDispatch(
            stringLens + 1,
            dispatchIndices + 1,
            rowsStart,
            rowsByteSize,
            (uint8_t)nbColumns,
            sep);
    ZL_RET_R_IF_ERR(rep);
    size_t actualNbStrs = ZL_validResult(rep);
    actualNbStrs += 1;                            // +1 for header
    dispatchIndices[0] = (uint16_t)nbColumns + 1; // header

    // return
    retLexResult->stringLens      = stringLens;
    retLexResult->dispatchIndices = dispatchIndices;
    retLexResult->nbStrs          = actualNbStrs;
    retLexResult->nbColumns       = nbColumns;

    return ZL_returnSuccess();
}
