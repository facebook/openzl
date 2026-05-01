// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "parquet_metadata.h"

#include <stack>
#include <stdexcept>
#include <tuple>

namespace zstrong {
namespace parquet {
namespace {

using ColumnChunks = std::vector<ColumnChunkMetadata>;

void throwIfTTypeNE(TType actual, TType expected)
{
    if (actual != expected) {
        throw std::runtime_error("Unexpected type!");
    }
}

void throwIfLargeAlloc(size_t size, size_t max)
{
    if (size > max) {
        throw std::runtime_error("Allocating too much memory!");
    }
}

DataType getDataType(int32_t val)
{
    switch (val) {
        case 0:
            return DataType::BOOLEAN;
        case 1:
            return DataType::INT32;
        case 2:
            return DataType::INT64;
        case 4:
            return DataType::FLOAT;
        case 5:
            return DataType::DOUBLE;
        case 6:
            return DataType::BYTE_ARRAY;
        case 7:
            return DataType::FIXED_LEN_BYTE_ARRAY;
        default:
            throw std::runtime_error("Invalid Parquet Data Type!");
    };
};

PageType getPageType(int32_t val)
{
    switch (val) {
        case 0:
            return PageType::DATA_PAGE;
        case 1:
            return PageType::INDEX_PAGE;
        case 2:
            return PageType::DICTIONARY_PAGE;
        case 4:
            return PageType::DATA_PAGE_V2;
        default:
            throw std::runtime_error("Invalid Parquet Page Type!");
    };
};

Encoding getEncoding(int32_t val)
{
    switch (val) {
        case 0:
            return Encoding::PLAIN;
        case 2:
            return Encoding::PLAIN_DICTIONARY;
        case 3:
            return Encoding::RLE;
        case 4:
            return Encoding::BIT_PACKED;
        case 5:
            return Encoding::DELTA_BINARY_PACKED;
        case 6:
            return Encoding::DELTA_LENGTH_BYTE_ARRAY;
        case 7:
            return Encoding::DELTA_BYTE_ARRAY;
        case 8:
            return Encoding::RLE_DICTIONARY;
        case 9:
            return Encoding::BYTE_STREAM_SPLIT;
        default:
            throw std::runtime_error("Invalid Parquet Encoding Type!");
    };
};

uint32_t readColumnChunkMetadata(
        ThriftCompactReader& reader,
        ColumnChunkMetadata& metadata)
{
    uint32_t read = 0;
    read += reader.readStructBegin();
    while (true) {
        TType type;
        int16_t fieldId;
        read += reader.readFieldBegin(type, fieldId);

        if (type == TType::T_STOP)
            break;

        switch (fieldId) {
            case 1: /* Type */
            {
                throwIfTTypeNE(type, TType::T_I32);
                int32_t datatype;
                read += reader.readI32(datatype);
                metadata.type = getDataType(datatype);
                break;
            }
            case 3: /* Path in Schema */
            {
                throwIfTTypeNE(type, TType::T_LIST);
                TType elem_type;
                uint32_t size;
                read += reader.readListBegin(elem_type, size);
                throwIfTTypeNE(elem_type, TType::T_STRING);

                throwIfLargeAlloc(size, reader.getRemaining());
                metadata.path_in_schema = std::vector<std::string>(size);
                for (uint32_t i = 0; i < size; ++i) {
                    read += reader.readString(metadata.path_in_schema.at(i));
                }
                read += reader.readListEnd();
                break;
            }
            case 4: /* Compression Codec */
            {
                throwIfTTypeNE(type, TType::T_I32);
                int32_t codec;
                read += reader.readI32(codec);
                if (codec != 0) {
                    throw std::runtime_error("Found compressed chunk!");
                }
                break;
            }
            case 6: /* Total Uncompressed Size */
            {
                throwIfTTypeNE(type, TType::T_I64);
                read += reader.readI64(metadata.numBytes);
                break;
            }
            default:
                read += reader.skip(type);
                break;
        }
    }
    read += reader.readStructEnd();
    return read;
}

uint32_t readColumnChunk(
        ThriftCompactReader& reader,
        ColumnChunkMetadata& metadata)
{
    uint32_t read = 0;
    read += reader.readStructBegin();
    while (true) {
        TType type;
        int16_t fieldId;
        read += reader.readFieldBegin(type, fieldId);

        if (type == TType::T_STOP)
            break;

        switch (fieldId) {
            case 3: /* Column Metadata */
            {
                throwIfTTypeNE(type, TType::T_STRUCT);
                read += readColumnChunkMetadata(reader, metadata);
                break;
            }
            default:
                read += reader.skip(type);
                break;
        }
    }
    read += reader.readStructEnd();
    return read;
}

uint32_t
readRowGroup(ThriftCompactReader& reader, FileMetadata& metadata, uint32_t row)
{
    uint32_t read = 0;
    read += reader.readStructBegin();
    while (true) {
        TType type;
        int16_t fieldId;
        read += reader.readFieldBegin(type, fieldId);

        if (type == TType::T_STOP)
            break;

        switch (fieldId) {
            case 1: /* Column Chunks */
            {
                throwIfTTypeNE(type, TType::T_LIST);
                TType elem_type;

                read += reader.readListBegin(elem_type, metadata.numColumns);
                throwIfTTypeNE(elem_type, TType::T_STRUCT);
                // Initialize Column Chunks
                if (row == 0) {
                    size_t numChunks =
                            metadata.numColumns * metadata.numRowGroups;
                    throwIfLargeAlloc(numChunks, reader.getRemaining());
                    metadata.columnChunks = ColumnChunks(numChunks);
                }
                for (uint32_t i = 0; i < metadata.numColumns; ++i) {
                    auto idx = row * metadata.numColumns + i;
                    read += readColumnChunk(
                            reader, metadata.columnChunks.at(idx));
                }
                read += reader.readListEnd();
                break;
            }
            default:
                read += reader.skip(type);
                break;
        }
    }
    read += reader.readStructEnd();
    return read;
}

uint32_t readDataPageHeader(ThriftCompactReader& reader, PageHeader& header)
{
    uint32_t read = 0;
    read += reader.readStructBegin();
    while (true) {
        TType type{};
        int16_t fieldId{};
        read += reader.readFieldBegin(type, fieldId);

        if (type == TType::T_STOP)
            break;

        switch (fieldId) {
            case 2: /* Encoding */ {
                throwIfTTypeNE(type, TType::T_I32);
                int32_t encoding{};
                read += reader.readI32(encoding);
                header.encoding = getEncoding(encoding);
                break;
            }
            case 3: /* Definition Level Encoding */ {
                throwIfTTypeNE(type, TType::T_I32);
                int32_t encoding{};
                read += reader.readI32(encoding);
                header.dl_encoding = getEncoding(encoding);
                break;
            }
            case 4: /* Repetition Level Encoding */ {
                throwIfTTypeNE(type, TType::T_I32);
                int32_t encoding{};
                read += reader.readI32(encoding);
                header.rl_encoding = getEncoding(encoding);
                break;
            }
            default:
                read += reader.skip(type);
                break;
        }
    }
    read += reader.readStructEnd();
    return read;
}

enum class RepetitionType : uint32_t {
    REQUIRED = 0,
    OPTIONAL = 1,
    REPEATED = 2,
};

RepetitionType getRepetitionType(int32_t val)
{
    switch (val) {
        case 0:
            return RepetitionType::REQUIRED;
        case 1:
            return RepetitionType::OPTIONAL;
        case 2:
            return RepetitionType::REPEATED;
        default:
            throw std::runtime_error("Invalid Parquet Repetition Type!");
    }
}

struct SchemaElement {
    std::string name;
    bool isLeaf = false;
    /// Populated for leaf nodes
    DataType type;
    int32_t typeWidth = 0;
    /// Repetition type of this element. Defaults to REQUIRED, which is the
    /// default for the root element in the parquet schema.
    RepetitionType repetitionType = RepetitionType::REQUIRED;

    // Populated for non-leaf nodes
    int32_t numChildren = 0;
};

uint32_t readSchemaElement(ThriftCompactReader& reader, SchemaElement& e)
{
    uint32_t read = 0;
    read += reader.readStructBegin();
    while (true) {
        TType type{};
        int16_t fieldId{};
        read += reader.readFieldBegin(type, fieldId);

        if (type == TType::T_STOP)
            break;

        switch (fieldId) {
            case 1: /* Type */ {
                throwIfTTypeNE(type, TType::T_I32);
                int32_t datatype;
                read += reader.readI32(datatype);
                e.type = getDataType(datatype);
                // Type is only populated for leaf nodes
                e.isLeaf = true;
                break;
            }
            case 2: /* Type Length */ {
                throwIfTTypeNE(type, TType::T_I32);
                read += reader.readI32(e.typeWidth);
                break;
            }
            case 3: /* Repetition Type */ {
                throwIfTTypeNE(type, TType::T_I32);
                int32_t reptype;
                read += reader.readI32(reptype);
                e.repetitionType = getRepetitionType(reptype);
                break;
            }
            case 4: /* Name */ {
                throwIfTTypeNE(type, TType::T_STRING);
                read += reader.readString(e.name);
                break;
            }
            case 5: /* Num Children */ {
                throwIfTTypeNE(type, TType::T_I32);
                read += reader.readI32(e.numChildren);
                break;
            }
            default:
                read += reader.skip(type);
                break;
        }
    }
    read += reader.readStructEnd();
    return read;
}

void populateSchemaMetadata(
        std::vector<SchemaElement>& schemaElements,
        std::map<SchemaPath, SchemaMetadata>& schemaMetadata)
{
    if (schemaElements.empty()) {
        return;
    }
    // Stack tuple: remaining children under this parent, the parent's path,
    // and whether any ancestor on this path is OPTIONAL/REPEATED (definition
    // levels) or REPEATED (repetition levels).
    std::stack<std::tuple<int32_t, SchemaPath, bool, bool>> paths;
    paths.emplace(
            schemaElements.front().numChildren, SchemaPath(), false, false);
    schemaElements.erase(schemaElements.begin());

    for (auto& e : schemaElements) {
        if (paths.empty()) {
            throw std::runtime_error("Invalid schema!");
        }
        auto& [numChildren, parentPath, parentHasDef, parentHasRep] =
                paths.top();
        auto path = parentPath;
        path.push_back(e.name);
        bool hasDef =
                parentHasDef || (e.repetitionType != RepetitionType::REQUIRED);
        bool hasRep =
                parentHasRep || (e.repetitionType == RepetitionType::REPEATED);
        if (--numChildren == 0) {
            paths.pop(); // invalidates parent* references above
        }

        if (!e.isLeaf) {
            paths.emplace(e.numChildren, path, hasDef, hasRep);
            continue;
        }

        SchemaMetadata m = {
            .type                = e.type,
            .typeWidth           = (uint32_t)e.typeWidth,
            .hasDefinitionLevels = hasDef,
            .hasRepetitionLevels = hasRep,
        };

        auto it = schemaMetadata.emplace(SchemaPath(path), m);
        if (!it.second) {
            throw std::runtime_error("Duplicate schema path!");
        }
    }
}

} // namespace

uint32_t readFileMetadata(ThriftCompactReader& reader, FileMetadata& metadata)
{
    uint32_t read = 0;
    read += reader.readStructBegin();
    while (true) {
        TType type{};
        int16_t fieldId{};
        read += reader.readFieldBegin(type, fieldId);

        if (type == TType::T_STOP)
            break;

        switch (fieldId) {
            case 2: /* Schema Element */
            {
                throwIfTTypeNE(type, TType::T_LIST);
                TType elem_type;
                uint32_t size;

                read += reader.readListBegin(elem_type, size);
                throwIfLargeAlloc(size, reader.getRemaining());
                throwIfTTypeNE(elem_type, TType::T_STRUCT);

                std::vector<SchemaElement> schemaElements(size);
                for (uint32_t i = 0; i < size; ++i) {
                    read += readSchemaElement(reader, schemaElements.at(i));
                }
                populateSchemaMetadata(schemaElements, metadata.schemaMetadata);
                read += reader.readListEnd();
                break;
            }
            case 3: /* Num Rows */
            {
                throwIfTTypeNE(type, TType::T_I64);
                int64_t numRows{};
                read += reader.readI64(numRows);
                metadata.numRows = numRows;
                break;
            }
            case 4: /* Row Groups */
            {
                throwIfTTypeNE(type, TType::T_LIST);
                TType elem_type{};

                read += reader.readListBegin(elem_type, metadata.numRowGroups);
                throwIfTTypeNE(elem_type, TType::T_STRUCT);
                for (uint32_t i = 0; i < metadata.numRowGroups; ++i) {
                    read += readRowGroup(reader, metadata, i);
                }
                read += reader.readListEnd();
                break;
            }
            default:
                read += reader.skip(type);
                break;
        }
    }
    read += reader.readStructEnd();
    return read;
}

uint32_t readPageHeader(ThriftCompactReader& reader, PageHeader& header)
{
    uint32_t read = 0;
    read += reader.readStructBegin();
    while (true) {
        TType type{};
        int16_t fieldId{};
        read += reader.readFieldBegin(type, fieldId);

        if (type == TType::T_STOP)
            break;

        switch (fieldId) {
            case 1: { /* Page Type */
                throwIfTTypeNE(type, TType::T_I32);
                int32_t pageType{};
                read += reader.readI32(pageType);
                header.pageType = getPageType(pageType);
                break;
            }
            case 2: { /* Uncompressed Page Size */
                throwIfTTypeNE(type, TType::T_I32);
                read += reader.readI32(header.numBytes);
                break;
            }
            case 5: { /* Data Page Header */
                throwIfTTypeNE(type, TType::T_STRUCT);
                read += readDataPageHeader(reader, header);
                break;
            }
            default:
                read += reader.skip(type);
                break;
        }
    }
    read += reader.readStructEnd();
    return read;
}

} // namespace parquet
} // namespace zstrong
