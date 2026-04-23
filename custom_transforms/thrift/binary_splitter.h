// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "custom_transforms/thrift/constants.h"      // @manual
#include "custom_transforms/thrift/parse_config.h"   // @manual
#include "custom_transforms/thrift/split_helpers.h"  // @manual
#include "custom_transforms/thrift/splitter.h"       // @manual
#include "custom_transforms/thrift/thrift_errors.h"  // @manual
#include "custom_transforms/thrift/thrift_parsers.h" // @manual
#include "custom_transforms/thrift/thrift_types.h"   // @manual

#include <folly/Range.h>

namespace zstrong::thrift {

class BinaryParser : public BaseParser<BinaryParser> {
    friend class BaseParser<BinaryParser>;

   public:
    using BaseParser::BaseParser;

   private:
    // Consume at least one byte from rs_
    inline void advance(const PT::Iterator& current);
    inline void parseTulipV2Header(const PT::Iterator& current);

    inline PT::Iterator parseFieldHeader(
            const PT::Iterator& struct_it,
            int16_t prevId);
    inline ListInfo parseListHeader(const PT::Iterator& current);
    inline MapInfo parseMapHeader(const PT::Iterator& current);

    template <typename Value>
    Value readValue();
};

class DBinaryParser : public DBaseParser<DBinaryParser> {
    friend class DBaseParser<DBinaryParser>;

   public:
    using DBaseParser::DBaseParser;

   private:
    // Consume at least one byte from readStreamSet_
    inline void advance(const PT::Iterator& current);
    inline PT::Iterator unparseFieldHeader(
            const PT::Iterator& struct_it,
            int16_t prevId);
    inline ListInfo unparseListHeader(const PT::Iterator& current);
    inline MapInfo unparseMapHeader(const PT::Iterator& current);

    template <typename Value>
    void writeValue(Value val);
};

template <typename Value>
ZL_FORCE_INLINE_ATTR Value BinaryParser::readValue()
{
    // TODO(felixh): make this robust??
    return folly::Endian::big(rs_.readValue<Value>());
}

template <typename Value>
ZL_FORCE_INLINE_ATTR void DBinaryParser::writeValue(Value val)
{
    assert(ws_.width() == 1);
    ws_.writeValue(folly::Endian::big(val));
}

ZL_FORCE_INLINE_ATTR ListInfo
BinaryParser::parseListHeader(const BinaryParser::PT::Iterator& current)
{
    const auto elemType = (TType)readValue<uint8_t>();
    if (elemType > TType::T_FLOAT) {
        throw ThriftMalformedInputError("Illegal list element type!");
    }
    writeType(elemType);

    const auto size = readValue<uint32_t>();
    writeValue<uint32_t>(current.lengths(), size);

    debug("List header: size {}, type {}", size, elemType);
    return { .size = size, .elemType = elemType };
}

ZL_FORCE_INLINE_ATTR ListInfo
DBinaryParser::unparseListHeader(const DBinaryParser::PT::Iterator& current)
{
    const auto elemType = readType();
    if (elemType > TType::T_FLOAT) {
        throw ThriftMalformedInputError("Illegal list element type!");
    }
    writeValue((uint8_t)elemType);

    const auto size = readValue<uint32_t>(current.lengths());
    writeValue(size);

    debug("List header: size {}, type {} ({})",
          size,
          thriftTypeToString(elemType),
          elemType);
    return { .size = size, .elemType = elemType };
}

ZL_FORCE_INLINE_ATTR MapInfo
BinaryParser::parseMapHeader(const BinaryParser::PT::Iterator& current)
{
    const auto keyType   = (TType)readValue<uint8_t>();
    const auto valueType = (TType)readValue<uint8_t>();
    if (keyType > TType::T_FLOAT) {
        throw ThriftMalformedInputError("Illegal map key type!");
    }
    if (valueType > TType::T_FLOAT) {
        throw ThriftMalformedInputError("Illegal map value type!");
    }
    writeType(keyType);
    writeType(valueType);

    const auto size = readValue<uint32_t>();
    writeValue(current.lengths(), size);

    debug("Map header: size {}, keyType {} ({}), valueType {} ({})",
          size,
          thriftTypeToString(keyType),
          keyType,
          thriftTypeToString(valueType),
          valueType);
    return { .size = size, .keyType = keyType, .valueType = valueType };
}

ZL_FORCE_INLINE_ATTR MapInfo
DBinaryParser::unparseMapHeader(const DBinaryParser::PT::Iterator& current)
{
    const auto keyType   = readType();
    const auto valueType = readType();
    if (keyType > TType::T_FLOAT) {
        throw ThriftMalformedInputError("Illegal map key type!");
    }
    if (valueType > TType::T_FLOAT) {
        throw ThriftMalformedInputError("Illegal map value type!");
    }
    writeValue((uint8_t)keyType);
    writeValue((uint8_t)valueType);

    const auto size = readValue<uint32_t>(current.lengths());
    writeValue(size);

    debug("Map header: size {}, keyType {}, valueType {}",
          size,
          keyType,
          valueType);
    return { .size = size, .keyType = keyType, .valueType = valueType };
}

ZL_FORCE_INLINE_ATTR BinaryParser::PT::Iterator BinaryParser::parseFieldHeader(
        const BinaryParser::PT::Iterator& struct_it,
        int16_t prevId)
{
    const auto type = (TType)readValue<uint8_t>();
    if (type > TType::T_FLOAT) {
        throw ThriftMalformedInputError("Illegal type!");
    }
    writeType(type);

    // TType::T_STOP is a special case, there is no field id
    if (type == TType::T_STOP) {
        return struct_it.stop();
    }

    const auto rawId = readValue<int16_t>();
    const auto id    = static_cast<ThriftNodeId>(rawId);

    // Apply delta transform and write to output stream
    const uint16_t rawIdDelta = (uint16_t)(rawId) - (uint16_t)(prevId);
    writeFieldDelta(rawIdDelta);

    auto field_it = struct_it.child(id, type);

    debug("Field header: type {}, id {}", type, rawId);
    return field_it;
}

ZL_FORCE_INLINE_ATTR DBinaryParser::PT::Iterator
DBinaryParser::unparseFieldHeader(
        const DBinaryParser::PT::Iterator& struct_it,
        int16_t prevId)
{
    // Get the type
    const auto type = readType();
    if (type > TType::T_FLOAT) {
        throw ThriftMalformedInputError("Illegal type!");
    }
    writeValue((uint8_t)type);

    // TType::T_STOP is a special case, there is no field id
    if (type == TType::T_STOP) {
        return struct_it.stop();
    }

    // Get the field id
    const uint16_t rawIdDelta = readFieldDelta();
    const int16_t rawId       = (int16_t)(rawIdDelta + (uint16_t)(prevId));
    writeValue(rawId);

    const auto id = ThriftNodeId(rawId);
    auto field_it = struct_it.child(id, type);

    debug("Field header: type {}, id {}", type, rawId);
    return field_it;
}

void BinaryParser::advance(const BinaryParser::PT::Iterator& current)
{
    const auto type = current.type();
    const auto id   = current.id();
    debug("Advancing: pos {}, path {}, type {} ({}), id {}",
          rs_.pos(),
          current.pathStr(),
          thriftTypeToString(type),
          type,
          (int32_t)id);
    switch (type) {
        case TType::T_BOOL: {
            const auto val = readValue<uint8_t>();
            writeValue(current, val);
            break;
        }
        case TType::T_BYTE: {
            const auto val = readValue<int8_t>();
            writeValue(current, val);
            break;
        }
        case TType::T_I16: {
            const auto val = readValue<int16_t>();
            writeValue(current, val);
            break;
        }
        case TType::T_I32: {
            const auto val = readValue<int32_t>();
            writeValue(current, val);
            break;
        }
        case TType::T_I64: {
            const auto val = readValue<int64_t>();
            writeValue(current, val);
            break;
        }
        case TType::T_FLOAT: {
            const auto val = readValue<float>();
            writeValue(current, val);
            break;
        }
        case TType::T_DOUBLE: {
            const auto val = readValue<double>();
            writeValue(current, val);
            break;
        }
        case TType::T_STRING: {
            const auto len = readValue<uint32_t>();
            writeValue(current.lengths(), len);
            const folly::ByteRange bytes = readBytes(len);
            writeBytes(current, bytes);
            break;
        }
        case TType::T_MAP: {
            parseMap(current);
            break;
        }
        case TType::T_SET:
        case TType::T_LIST: {
            parseList(current);
            break;
        }
        case TType::T_STRUCT: {
            int16_t prevId = 0;
            while (1) {
                const auto it = parseFieldHeader(current, prevId);
                if (it.type() == TType::T_STOP)
                    break;
                advance(it);
                prevId = folly::to<int16_t>(it.id());
            }
            break;
        }
        case TType::T_STOP:
        case TType::T_VOID:
        case TType::T_U16:
        case TType::T_U32:
        case TType::T_U64:
        case TType::T_UTF8:
        case TType::T_UTF16:
        case TType::T_STREAM:
        default: {
            throw ThriftMalformedInputError(
                    "Unexpected thrift type: " + thriftTypeToString(type));
        }
    }
}

void DBinaryParser::advance(const DBinaryParser::PT::Iterator& current)
{
    const auto type = current.type();
    const auto id   = current.id();
    debug("Advancing: pos {}, path {}, type {} ({}), id {}",
          ws_.nbytes(),
          current.pathStr(),
          thriftTypeToString(type),
          type,
          (int32_t)id);
    switch (type) {
        case TType::T_BOOL: {
            const auto val = readValue<uint8_t>(current);
            writeValue(val);
            break;
        }
        case TType::T_BYTE: {
            const auto val = readValue<int8_t>(current);
            writeValue(val);
            break;
        }
        case TType::T_I16: {
            const auto val = readValue<int16_t>(current);
            writeValue(val);
            break;
        }
        case TType::T_I32: {
            const auto val = readValue<int32_t>(current);
            writeValue(val);
            break;
        }
        case TType::T_I64: {
            const auto val = readValue<int64_t>(current);
            writeValue(val);
            break;
        }
        case TType::T_FLOAT: {
            const auto val = readValue<float>(current);
            writeValue(val);
            break;
        }
        case TType::T_DOUBLE: {
            const auto val = readValue<double>(current);
            writeValue(val);
            break;
        }
        case TType::T_STRING: {
            const auto len = readValue<uint32_t>(current.lengths());
            writeValue(len);
            copyBytes(current.stream(), len);
            break;
        }
        case TType::T_MAP: {
            unparseMap(current);
            break;
        }
        case TType::T_SET:
        case TType::T_LIST: {
            unparseList(current);
            break;
        }
        case TType::T_STRUCT: {
            int16_t prevId = 0;
            while (1) {
                const auto it = unparseFieldHeader(current, prevId);
                if (it.type() == TType::T_STOP)
                    break;
                advance(it);
                prevId = int16_t(it.id());
            }
            break;
        }
        case TType::T_STOP:
        case TType::T_VOID:
        case TType::T_U16:
        case TType::T_U32:
        case TType::T_U64:
        case TType::T_UTF8:
        case TType::T_UTF16:
        case TType::T_STREAM:
        default: {
            throw ThriftMalformedInputError(
                    "Unexpected thrift type: " + thriftTypeToString(type));
        }
    }
}

void BinaryParser::parseTulipV2Header(const PT::Iterator&)
{
    throw InvalidConfigError{
        "TulipV2 mode is not compatible with binary protocol!"
    };
}

} // namespace zstrong::thrift
