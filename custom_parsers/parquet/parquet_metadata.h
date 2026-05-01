// Copyright (c) Meta Platforms, Inc. and affiliates.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include "custom_parsers/parquet/thrift_compact_reader.h"

namespace zstrong {
namespace parquet {

enum class DataType : uint32_t {
    BOOLEAN = 0,
    INT32   = 1,
    INT64   = 2,
    // INT96      = 3, // deprecated
    FLOAT                = 4,
    DOUBLE               = 5,
    BYTE_ARRAY           = 6,
    FIXED_LEN_BYTE_ARRAY = 7,
};

using SchemaPath = std::vector<std::string>;

struct ColumnChunkMetadata {
    /// The type of the data
    DataType type;
    /// The uncompressed size of the chunk
    int64_t numBytes;
    /// The schema path
    SchemaPath path_in_schema;
};

struct SchemaMetadata {
    /// The type of the data
    DataType type = (DataType)-1;
    /// The size of the data type in bytes
    uint32_t typeWidth = 0;
    /// True when this leaf has any OPTIONAL or REPEATED ancestor; data pages
    /// for the column then contain a definition-level block.
    bool hasDefinitionLevels = false;
    /// True when this leaf has any REPEATED ancestor; data pages for the
    /// column then contain a repetition-level block.
    bool hasRepetitionLevels = false;
};

struct FileMetadata {
    /// The number of rows in the file.
    uint64_t numRows = 0;
    /// The number of columns in the file.
    uint32_t numColumns = 0;
    /// The number of row groups in the file.
    uint32_t numRowGroups = 0;
    // Column Chunk information
    std::vector<ColumnChunkMetadata> columnChunks;
    /// Schema information
    std::map<SchemaPath, SchemaMetadata> schemaMetadata;
};

uint32_t readFileMetadata(ThriftCompactReader& reader, FileMetadata& metadata);

enum class PageType : uint32_t {
    DATA_PAGE       = 0,
    INDEX_PAGE      = 1,
    DICTIONARY_PAGE = 2,
    DATA_PAGE_V2    = 3,
};

enum class Encoding : uint32_t {
    PLAIN = 0,
    //  GROUP_VAR_INT = 1; // deprecated
    PLAIN_DICTIONARY        = 2,
    RLE                     = 3,
    BIT_PACKED              = 4,
    DELTA_BINARY_PACKED     = 5,
    DELTA_LENGTH_BYTE_ARRAY = 6,
    DELTA_BYTE_ARRAY        = 7,
    RLE_DICTIONARY          = 8,
    BYTE_STREAM_SPLIT       = 9,
};

struct PageHeader {
    /// The page type
    PageType pageType;
    /// The page size
    int32_t numBytes;
    /// The page encoding
    Encoding encoding;
    Encoding dl_encoding;
    Encoding rl_encoding;
};

/**
 * Populates the page header with the values read from the reader.
 *
 * @param reader The reader to read values from.
 * @param metadata The page header to populate.
 * @returns The number of bytes read from the reader.
 */
uint32_t readPageHeader(ThriftCompactReader& reader, PageHeader& metadata);

} // namespace parquet
} // namespace zstrong
