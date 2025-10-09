// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef CUSTOM_PARSERS_ZIP_LEXER_H
#define CUSTOM_PARSERS_ZIP_LEXER_H

#include <stddef.h>
#include <stdint.h>

#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"

ZL_BEGIN_C_DECLS

typedef struct ZL_ZipLexer_s ZL_ZipLexer;

/**
 * The type of a token in a zip file, corresponding to the Zip spec.
 */
typedef enum {
    ZL_ZipTokenType_LocalFileHeader,
    ZL_ZipTokenType_CompressedData,
    ZL_ZipTokenType_DataDescriptor,
    ZL_ZipTokenType_CentralDirectory,
    ZL_ZipTokenType_Zip64EndOfCentralDirectoryRecord,
    ZL_ZipTokenType_Zip64EndOfCentralDirectoryLocator,
    ZL_ZipTokenType_EndOfCentralDirectoryRecord,
    ZL_ZipTokenType_Unknown,
} ZL_ZipTokenType;

typedef struct {
    /// Pointer to the beginning of the token in the source buffer.
    const char* ptr;
    size_t size;           //< Size of the token in bytes.
    ZL_ZipTokenType type; //< Type of the token.
    /// Compression method for the file, or 0 if not a file.
    uint16_t compressionMethod;
    /// Size of the filename, or 0 if not a file.
    uint16_t filenameSize;
    /// Pointer to the filename, or NULL if not a file.
    /// @warning Not zero terminated.
    const char* filename;
} ZL_ZipToken;

/**
 * Initializes a Zip lexer on the given input buffer. The lexer allows for
 * garbage data before & after the zip file.
 *
 * @returns Success if the input buffer may be a valid zip file,
 *          or an error code if the input file is definitely not
 *          a supported zip file.
 *
 * @note This lexer supports all zip files whose central directory
 *       is listed in order of occurrence in the file.
 */
ZL_Report
ZL_ZipLexer_init(ZL_ZipLexer* lexer, const void* src, size_t srcSize);

/**
 * Initializes a Zip lexer with a known offset to the EOCD.
 * @see ZL_ZipLexer_init()
 */
ZL_Report ZL_ZipLexer_initWithEOCD(
        ZL_ZipLexer* lexer,
        const void* src,
        size_t srcSize,
        size_t eocdOffset);

/**
 * Lexes the next @p outCapacity tokens from the input buffer.
 *
 * @returns The number of tokens lexed, or an error code.
 *          Upon success, the input may be a valid zip file,
 *          and upon error the input is definitely not a supported
 *          zip file. Once it returns a value < @p outCapacity,
 *          the input has been fully lexed, and it will return 0
 *          on subsequent calls.
 */
ZL_Report
ZL_ZipLexer_lex(ZL_ZipLexer* lexer, ZL_ZipToken* out, size_t outCapacity);

/// @returns true if the lexer has finished lexing the source.
bool ZL_ZipLexer_finished(const ZL_ZipLexer* lexer);

size_t ZL_ZipLexer_expectedNumTokens(const ZL_ZipLexer* lexer);

/**
 * @returns the number of files in the zip file.
 * @note If the zip file is corrupt, this may report an incorrect value,
 * however it is validated that the number of files is at least plausible,
 * and is no more than the source size / 76.
 */
size_t ZL_ZipLexer_numFiles(const ZL_ZipLexer* lexer);

/// @returns true if the input buffer is likely a zip file.
bool ZL_isLikelyZipFile(const void* src, size_t srcSize);

/// @section Implementation details

typedef struct {
    const char* localFileHeaderPtr;
    size_t localFileHeaderSize;
    const char* compressedDataPtr;
    uint64_t compressedDataSize;
    const char* dataDescriptorPtr;
    size_t dataDescriptorSize;

    uint16_t compressionMethod;
    uint16_t filenameSize;
    const char* filename; //< Not zero terminated, may be NULL.
} ZL_ZipLexer_FileState;

struct ZL_ZipLexer_s {
    /// Pointer to the current position in the input buffer.
    /// Everything before this pointer has already been lexed.
    const char* srcPtr;
    /// Pointer to the end of the input buffer.
    const char* srcEnd;
    /// Pointer to the beginning of the zip file.
    /// It may not be the beginning of the input buffer, if the lexer detects
    /// that there is garbage before the zip file.
    const char* zipBegin;
    /// Pointer to the beginning of the current Central Directory File Header
    const char* cdfhPtr;
    /// Pointer to the end of the Central Directory File Headers
    const char* cdfhEnd;
    /// Index of the current Central Directory File Header
    size_t cdfhIdx;
    /// Number of Central Directory File Headers
    size_t cdfhNum;

    // Pointers & sizes for the trailing metadata sections.
    // They are set to NULL if they are not present, or if
    // they have already been lexed.
    const char* centralDirectoryPtr;
    const char* zip64EndOfCentralDirectoryRecordPtr;
    uint64_t zip64EndOfCentralDirectoryRecordSize;
    const char* zip64EndOfCentralDirectoryLocatorPtr;
    const char* endOfCentralDirectoryRecordPtr;
    uint32_t endOfCentralDirectoryRecordSize;

    ZL_ZipLexer_FileState fileState;
};

ZL_END_C_DECLS

#endif
