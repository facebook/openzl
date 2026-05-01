// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/parquet/parquet_lexer.h"
#include "custom_parsers/parquet/parquet_metadata.h"
#include "custom_parsers/parquet/thrift_compact_reader.h"
#include "openzl/common/allocation.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/xxhash.h"
#include "openzl/zl_errors.h"

#include <stdlib.h>
#include <exception>
#include <memory>
#include <string>
#include <vector>

using namespace zstrong::parquet;

struct ZL_ParquetLexer_s {
    /// Pointer to the current position in the input buffer.
    /// Everything before this pointer has already been lexed.
    const char* currPtr;
    /// Pointer to the footer of the input buffer.
    const char* footerPtr;
    /// Pointer to the end of the input buffer.
    const char* srcEnd;
    /// Pointer to the beginning of the input buffer.
    const char* srcBegin;
    /// The file metadata.
    std::unique_ptr<FileMetadata> fileMetadata;
    /// Whether or not we have already read the header magic
    bool readMagic = false;
    /// The current column chunk
    uint32_t chunkIdx = 0;
    /// The number of bytes read from the current chunk
    uint32_t chunkLexed = 0;
    /// The current page header. Will be reset after reading page
    /// data.
    std::unique_ptr<PageHeader> pageHeader = nullptr;
};

namespace {
const uint32_t kParquetMagic = 0x31524150;
const uint32_t kMinParquetSize =
        /* magics */ 2 * sizeof(kParquetMagic)
        + /* metadata length */ sizeof(uint32_t);

size_t getRemaining(const ZL_ParquetLexer* lexer)
{
    if (lexer->currPtr > lexer->footerPtr) {
        return 0;
    }
    return (size_t)(lexer->footerPtr - lexer->currPtr);
}

ColumnChunkMetadata& getChunkMeta(ZL_ParquetLexer* lexer)
{
    return lexer->fileMetadata->columnChunks.at(lexer->chunkIdx);
}

SchemaMetadata* getSchemaMeta(
        ZL_ParquetLexer* lexer,
        std::vector<std::string>& path)
{
    auto it = lexer->fileMetadata->schemaMetadata.find(path);
    if (it == lexer->fileMetadata->schemaMetadata.end()) {
        return nullptr;
    }
    return &it->second;
}

ZL_Report
lexMagic(ZL_ParquetLexer* lexer, ZL_ParquetToken* out, ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    ZL_ERR_IF_NE(
            ZL_readLE32(lexer->currPtr), kParquetMagic, node_invalid_input);
    out->type = ZL_ParquetTokenType_Magic;
    out->size = sizeof(kParquetMagic);
    out->ptr  = lexer->currPtr;
    lexer->currPtr += out->size;
    lexer->readMagic = true;
    return ZL_returnSuccess();
}

ZL_Report
lexFooter(ZL_ParquetLexer* lexer, ZL_ParquetToken* out, ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    size_t size = lexer->srcEnd - lexer->footerPtr;
    out->type   = ZL_ParquetTokenType_Footer;
    out->size   = size;
    out->ptr    = lexer->currPtr;
    lexer->currPtr += out->size;
    return ZL_returnSuccess();
}

/// Consume one RLE-encoded level block (4-byte LE size prefix + data) from the
/// current lexer position and shrink the page's remaining value-byte budget
/// accordingly.
ZL_Report consumeLevelData(
        ZL_ParquetLexer* lexer,
        ZL_ParquetToken* out,
        Encoding encoding,
        ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    ZL_ERR_IF_NE((int)encoding, (int)Encoding::RLE, node_invalid_input);
    ZL_ERR_IF_LT(getRemaining(lexer), 4, node_invalid_input);
    auto size = ZL_readLE32(lexer->currPtr);
    lexer->currPtr += 4;
    out->size += 4;
    ZL_ERR_IF_LT(getRemaining(lexer), size, node_invalid_input);
    lexer->currPtr += size;
    out->size += size;
    ZL_ERR_IF_LT(
            (size_t)lexer->pageHeader->numBytes, size + 4, node_invalid_input);
    lexer->pageHeader->numBytes -= size + 4;
    return ZL_returnSuccess();
}

ZL_Report lexPageHeader(
        ZL_ParquetLexer* lexer,
        ZL_ParquetToken* out,
        ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    out->type = ZL_ParquetTokenType_PageHeader;
    out->size = 0;
    out->ptr  = lexer->currPtr;

    auto advance = [&](size_t val) {
        lexer->currPtr += val;
        out->size += val;
    };

    ThriftCompactReader reader(lexer->currPtr, getRemaining(lexer));
    try {
        lexer->pageHeader = std::make_unique<PageHeader>();
        advance(readPageHeader(reader, *lexer->pageHeader));
    } catch (const std::exception& e) {
        return ZL_REPORT_ERROR(GENERIC, e.what());
    }

    // If we are in a data page, include the repetition and definition levels in
    // the header (if present).
    if (lexer->pageHeader->pageType == PageType::DATA_PAGE) {
        auto& chunkMeta = getChunkMeta(lexer);
        auto schemaMeta = getSchemaMeta(lexer, chunkMeta.path_in_schema);
        ZL_ERR_IF_NULL(schemaMeta, GENERIC, "Unknown schema path");

        if (schemaMeta->hasRepetitionLevels) {
            ZL_ERR_IF_ERR(consumeLevelData(
                    lexer, out, lexer->pageHeader->rl_encoding, errCtx));
        }
        if (schemaMeta->hasDefinitionLevels) {
            ZL_ERR_IF_ERR(consumeLevelData(
                    lexer, out, lexer->pageHeader->dl_encoding, errCtx));
        }
    }

    lexer->chunkLexed += out->size;
    return ZL_returnSuccess();
}

/**
 * Compute a tag for a given column chunk by hashing the schema path.
 */
uint32_t getTag(const std::vector<std::string>& path)
{
    XXH3_state_t state;
    XXH3_64bits_reset(&state);
    for (const auto& str : path) {
        size_t len = str.size();
        XXH3_64bits_update(&state, str.data(), len);
        XXH3_64bits_update(&state, &len, sizeof(size_t));
    }
    XXH64_hash_t result = XXH3_64bits_digest(&state);
    return (uint32_t)result;
}

ZL_Type getDataType(DataType type)
{
    switch (type) {
        case DataType::INT32:
        case DataType::INT64:
        case DataType::FLOAT:
        case DataType::DOUBLE:
            return ZL_Type_numeric;
        case DataType::BOOLEAN:
        case DataType::BYTE_ARRAY:
            return ZL_Type_serial;
        case DataType::FIXED_LEN_BYTE_ARRAY:
            return ZL_Type_struct;
    }
    assert(0);
    return ZL_Type_serial;
}

size_t getDataWidth(DataType type, int32_t width = 0)
{
    switch (type) {
        case DataType::INT32:
        case DataType::FLOAT:
            return 4;
        case DataType::INT64:
        case DataType::DOUBLE:
            return 8;
        case DataType::BOOLEAN:
        case DataType::BYTE_ARRAY:
            return 1;
        case DataType::FIXED_LEN_BYTE_ARRAY:
            return width;
    }
    assert(0);
    return 1;
}

ZL_Report lexDataPage(
        ZL_ParquetLexer* lexer,
        ZL_ParquetToken* out,
        ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    ZL_ERR_IF_NE(
            (int)lexer->pageHeader->encoding,
            (int)Encoding::PLAIN,
            node_invalid_input);
    out->type = ZL_ParquetTokenType_DataPage;
    out->size = lexer->pageHeader->numBytes;
    out->ptr  = lexer->currPtr;

    auto& chunkMeta = getChunkMeta(lexer);
    auto schemaMeta = getSchemaMeta(lexer, chunkMeta.path_in_schema);

    ZL_ERR_IF_NULL(schemaMeta, GENERIC, "Unknown schema path");
    ZL_ERR_IF_NE((uint32_t)schemaMeta->type, (uint32_t)chunkMeta.type, GENERIC);

    out->tag       = getTag(chunkMeta.path_in_schema);
    out->dataType  = getDataType(chunkMeta.type);
    out->dataWidth = getDataWidth(chunkMeta.type, schemaMeta->typeWidth);

    lexer->currPtr += out->size;
    lexer->chunkLexed += out->size;
    return ZL_returnSuccess();
}

ZL_Report
lexOne(ZL_ParquetLexer* lexer, ZL_ParquetToken* out, ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    if (!lexer->readMagic) {
        return lexMagic(lexer, out, errCtx);
    }

    if (lexer->currPtr == lexer->footerPtr) {
        return lexFooter(lexer, out, errCtx);
    }

    if (lexer->chunkIdx >= lexer->fileMetadata->columnChunks.size()) {
        ZL_ERR(GENERIC);
    }

    /// If we are not in the header or footer, we are in a column
    /// chunk. Check how many bytes are left in the current chunk
    /// and move onto the next one if needed.
    auto chunkRemaining = getChunkMeta(lexer).numBytes - lexer->chunkLexed;
    if (chunkRemaining == 0) {
        lexer->chunkIdx++;
        lexer->chunkLexed = 0;
    }
    ZL_ERR_IF_LT(chunkRemaining, 0, node_invalid_input);
    ZL_ERR_IF_LT(
            getRemaining(lexer), (size_t)chunkRemaining, node_invalid_input);

    if (!lexer->pageHeader) {
        return lexPageHeader(lexer, out, errCtx);
    }

    if (lexer->pageHeader->pageType == PageType::DATA_PAGE) {
        auto ret = lexDataPage(lexer, out, errCtx);
        lexer->pageHeader.reset();
        return ret;
    }

    return ZL_REPORT_ERROR(GENERIC, "Unknown page type");
}
} // namespace

ZL_ParquetLexer* ZL_ParquetLexer_create(void)
{
    ZL_ParquetLexer* const lexer =
            (ZL_ParquetLexer*)ZL_calloc(sizeof(ZL_ParquetLexer));
    if (lexer == NULL)
        return NULL;
    return lexer;
}

void ZL_ParquetLexer_free(ZL_ParquetLexer* lexer)
{
    if (lexer == NULL)
        return;
    lexer->fileMetadata.reset();
    lexer->pageHeader.reset();
    ZL_free(lexer);
}

ZL_Report ZL_ParquetLexer_init(
        ZL_ParquetLexer* lexer,
        const void* src,
        size_t srcSize,
        ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    ZL_ERR_IF_LT(srcSize, kMinParquetSize, node_invalid_input);
    lexer->srcBegin = (const char*)src;
    lexer->srcEnd   = lexer->srcBegin + srcSize;

    lexer->currPtr   = lexer->srcBegin;
    lexer->footerPtr = lexer->srcEnd;

    // Check magic
    ZL_ERR_IF_NE(
            ZL_readLE32(src),
            kParquetMagic,
            node_invalid_input,
            "Unknown magic!");

    // Check footer magic
    lexer->footerPtr -= sizeof(kParquetMagic);
    ZL_ERR_IF_NE(
            ZL_readLE32(lexer->footerPtr),
            kParquetMagic,
            node_invalid_input,
            "Unknown footer magic!");

    // Read metadata
    lexer->footerPtr -= sizeof(uint32_t);
    uint32_t metadataSize = ZL_readLE32(lexer->footerPtr);
    ZL_ERR_IF_LT(getRemaining(lexer), metadataSize, node_invalid_input);
    lexer->footerPtr -= metadataSize;

    ThriftCompactReader reader(lexer->footerPtr, metadataSize);

    try {
        lexer->fileMetadata = std::make_unique<FileMetadata>();
        auto readMeta       = readFileMetadata(reader, *lexer->fileMetadata);
        ZL_ERR_IF_NE(metadataSize, readMeta, GENERIC);
    } catch (const std::exception& e) {
        ZL_ERR(GENERIC, "Error while reading file metadata: %s", e.what());
    }

    return ZL_returnSuccess();
}

bool ZL_ParquetLexer_finished(const ZL_ParquetLexer* lexer)
{
    return lexer->currPtr == lexer->srcEnd;
}

ZL_Report ZL_ParquetLexer_lex(
        ZL_ParquetLexer* lexer,
        ZL_ParquetToken* out,
        size_t outCapacity,
        ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    size_t entries = 0;
    for (; !ZL_ParquetLexer_finished(lexer) && entries < outCapacity;
         ++entries) {
        ZL_ERR_IF_ERR(lexOne(lexer, out + entries, errCtx));
    }
    return ZL_returnValue(entries);
}

ZL_Report ZL_ParquetLexer_maxNumTokens(
        ZL_ParquetLexer* lexer,
        ZL_ErrorContext* errCtx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(errCtx);
    // TODO: Improve this estimate
    ZL_ERR_IF_LT(lexer->srcEnd, lexer->srcBegin, GENERIC);
    size_t srcSize = (size_t)(lexer->srcEnd - lexer->srcBegin);
    return ZL_returnValue(srcSize);
}
