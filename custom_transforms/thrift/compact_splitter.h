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

class CompactParser : public BaseParser<CompactParser> {
    friend class BaseParser<CompactParser>;

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

    static constexpr int64_t zigzagDecode(uint64_t n);
    static constexpr uint8_t parseBool(uint8_t byte);
    enum class TypeParse { kForCollection, kForField };
    static constexpr TType parseType(uint8_t rawType, TypeParse interp);
};

class DCompactParser : public DBaseParser<DCompactParser> {
    friend class DBaseParser<DCompactParser>;

   public:
    using DBaseParser::DBaseParser;

   private:
    // Consume at least one byte from readStreamSet_
    inline void advance(const PT::Iterator& current);
    inline ZL_FORCE_INLINE_ATTR bool advanceIfTrivial(
            const PT::Iterator& current);
    inline PT::Iterator unparseFieldHeader(
            const PT::Iterator& struct_it,
            int16_t prevId);
    inline ListInfo unparseListHeader(const PT::Iterator& current);
    inline MapInfo unparseMapHeader(const PT::Iterator& current);

    template <typename Value>
    void writeValue(Value val);

    inline static constexpr uint64_t zigzagEncode(int64_t n);
    inline static constexpr uint32_t zigzagEncode(int32_t n);
    inline static constexpr uint8_t unparseBool(uint8_t byte);
    inline static constexpr uint8_t unparseType(TType type);
};

ZL_FORCE_INLINE_ATTR constexpr int64_t CompactParser::zigzagDecode(uint64_t n)
{
    return (n >> 1) ^ -(n & 1);
}

ZL_FORCE_INLINE_ATTR constexpr uint64_t DCompactParser::zigzagEncode(int64_t n)
{
    return (n << 1) ^ (n >> 63);
}

ZL_FORCE_INLINE_ATTR constexpr uint32_t DCompactParser::zigzagEncode(int32_t n)
{
    return (n << 1) ^ (n >> 31);
}

ZL_FORCE_INLINE_ATTR constexpr uint8_t CompactParser::parseBool(uint8_t byte)
{
    constexpr uint8_t trueVal  = uint8_t(CType::CT_BOOLEAN_TRUE);
    constexpr uint8_t falseVal = uint8_t(CType::CT_BOOLEAN_FALSE);
    if (byte == trueVal || byte == falseVal) {
        return byte == trueVal;
    }
    throw ThriftMalformedInputError{ "Invalid boolean value!" };
}

ZL_FORCE_INLINE_ATTR constexpr uint8_t DCompactParser::unparseBool(uint8_t byte)
{
    if (byte == 0 || byte == 1) {
        return (uint8_t)(byte == 1 ? CType::CT_BOOLEAN_TRUE
                                   : CType::CT_BOOLEAN_FALSE);
    }
    throw ThriftMalformedInputError{ "Invalid boolean value!" };
}

ZL_FORCE_INLINE_ATTR constexpr TType CompactParser::parseType(
        uint8_t rawType,
        CompactParser::TypeParse interp)
{
    if (interp == TypeParse::kForCollection
        && rawType == 2 /* CT_BOOL_FALSE */) {
        // Reject non-canonical Thrift
        throw ThriftUnhandledInputError{
            "CT_BOOL_FALSE is not expected in collection headers"
        };
    }
    TType const type = CTypeToTType.at(rawType);
    if (type == TType::T_VOID) {
        throw ThriftMalformedInputError{ "T_VOID is not a valid wire value!" };
    }
    return type;
}

ZL_FORCE_INLINE_ATTR constexpr uint8_t DCompactParser::unparseType(TType type)
{
    static_assert(sizeof(TType) == sizeof(uint8_t));
    static_assert(sizeof(CType) == sizeof(uint8_t));
    const CType rawType = TTypeToCType.at(static_cast<uint8_t>(type));
    if (rawType == CType::CT_VOID) {
        throw ThriftMalformedInputError{ fmt::format(
                "Type value {} from the wire doesn't map to CType enum!",
                static_cast<uint8_t>(type)) };
    }
    return static_cast<uint8_t>(rawType);
}

template <typename Value>
ZL_FORCE_INLINE_ATTR Value CompactParser::readValue()
{
    static_assert(std::is_arithmetic_v<Value>);
    if (sizeof(Value) == 1 || std::is_floating_point_v<Value>) {
        // Note: this is a point of difference between Apache and FB Thrift.
        return folly::Endian::big(rs_.readValue<Value>());
    } else {
        assert(sizeof(Value) > 1 && std::is_integral_v<Value>);
        const uint64_t unsignedValue = rs_.readVarint();
        if (std::is_signed_v<Value>) {
            return folly::to<Value>(zigzagDecode(unsignedValue));
        } else {
            return folly::to<Value>(unsignedValue);
        }
    }
}

template <typename Value>
ZL_FORCE_INLINE_ATTR void DCompactParser::writeValue(Value val)
{
    assert(ws_.width() == 1);
    static_assert(std::is_arithmetic_v<Value>);
    if constexpr (sizeof(Value) == 1 || std::is_floating_point_v<Value>) {
        // Note: this is a point of difference between Apache and FB Thrift.
        ws_.writeValue(folly::Endian::big(val));
    } else {
        assert(sizeof(Value) > 1 && std::is_integral_v<Value>);
        if constexpr (std::is_signed_v<Value>) {
            ws_.writeVarint(zigzagEncode(val));
        } else {
            ws_.writeVarint(val);
        }
    }
}

ZL_FORCE_INLINE_ATTR ListInfo
CompactParser::parseListHeader(const CompactParser::PT::Iterator& current)
{
    const uint8_t byte = readValue<uint8_t>();

    // Split out the size
    uint32_t size;
    uint8_t const sizeNibble = byte >> 4;
    if (sizeNibble == 15) {
        size = readValue<uint32_t>();
        if (size < 15) {
            // Reject non-canonical Thrift
            throw ThriftUnhandledInputError{
                "Invalid list header: size < 15 but varint is present"
            };
        }
    } else {
        size = sizeNibble;
    }
    writeValue(current.lengths(), size);

    // Split out the element type
    const uint8_t typeNibble = byte & 0x0f;
    const TType elemType     = parseType(typeNibble, TypeParse::kForCollection);
    writeType(elemType);

    debug("List header: size {}, type {}", size, elemType);
    return { .size = size, .elemType = elemType };
}

ZL_FORCE_INLINE_ATTR ListInfo
DCompactParser::unparseListHeader(const DCompactParser::PT::Iterator& current)
{
    const uint32_t size      = readValue<uint32_t>(current.lengths());
    const TType elemType     = readType();
    const uint8_t typeNibble = unparseType(elemType);

    uint8_t sizeNibble = size < 15 ? (uint8_t)(size) : 15;
    assert(typeNibble <= 15); // guaranteed by unparseType
    uint8_t header = (uint8_t)(sizeNibble << 4) | typeNibble;
    writeValue<uint8_t>(header);
    if (size >= 15) {
        writeValue<uint32_t>(size);
    }

    debug("List header: size {}, type {} ({})",
          size,
          thriftTypeToString(elemType),
          elemType);
    return { .size = size, .elemType = elemType };
}

ZL_FORCE_INLINE_ATTR MapInfo
CompactParser::parseMapHeader(const CompactParser::PT::Iterator& current)
{
    const uint32_t size = readValue<uint32_t>();
    writeValue<uint32_t>(current.lengths(), size);

    if (size == 0) {
        return { .size      = 0,
                 .keyType   = TType::T_VOID,
                 .valueType = TType::T_VOID };
    }

    const uint8_t byte         = readValue<uint8_t>();
    const uint8_t rawKeyType   = byte >> 4;
    const uint8_t rawValueType = byte & 0x0f;
    const TType keyType   = parseType(rawKeyType, TypeParse::kForCollection);
    const TType valueType = parseType(rawValueType, TypeParse::kForCollection);
    writeType(keyType);
    writeType(valueType);

    debug("Map header: size {}, keyType {} ({}), valueType {} ({})",
          size,
          thriftTypeToString(keyType),
          keyType,
          thriftTypeToString(valueType),
          valueType);
    return { .size = size, .keyType = keyType, .valueType = valueType };
}

ZL_FORCE_INLINE_ATTR MapInfo
DCompactParser::unparseMapHeader(const DCompactParser::PT::Iterator& current)
{
    const auto size = readValue<uint32_t>(current.lengths());
    writeValue(size);

    if (size == 0) {
        return { .size      = 0,
                 .keyType   = TType::T_VOID,
                 .valueType = TType::T_VOID };
    }

    const TType keyType        = readType();
    const TType valueType      = readType();
    const uint8_t rawKeyType   = unparseType(keyType);
    const uint8_t rawValueType = unparseType(valueType);
    assert(rawKeyType <= 15);   // guaranteed by unparseType
    assert(rawValueType <= 15); // guaranteed by unparseType
    const uint8_t byte = (uint8_t)(rawKeyType << 4) | rawValueType;
    writeValue<uint8_t>(byte);

    debug("Map header: size {}, keyType {}, valueType {}",
          size,
          keyType,
          valueType);
    return { .size = size, .keyType = keyType, .valueType = valueType };
}

ZL_FORCE_INLINE_ATTR CompactParser::PT::Iterator
CompactParser::parseFieldHeader(
        const CompactParser::PT::Iterator& struct_it,
        int16_t prevId)
{
    const uint8_t byte = readValue<uint8_t>();

    // Split out the type
    const uint8_t typeNibble = (uint8_t)(byte & 0x0f);
    const TType type         = parseType(typeNibble, TypeParse::kForField);
    writeType(type);

    // TType::T_STOP is a special case, there is no field id
    if (type == TType::T_STOP) {
        if (byte != 0) {
            // Reject non-canonical Thrift
            throw ThriftUnhandledInputError{
                "Invalid field header: non-zero stop byte"
            };
        }
        return struct_it.stop();
    }

    // Decode field id and check for corruption
    int32_t wideId;
    const uint8_t deltaNibble = byte & 0xf0;
    if (deltaNibble == 0) {
        wideId = readValue<int16_t>();
    } else {
        const uint8_t delta = deltaNibble >> 4;
        wideId              = prevId + delta;
    }
    using Limits = std::numeric_limits<int16_t>;
    if (wideId < Limits::min() || wideId > Limits::max()) {
        throw std::range_error("Value out of range: " + std::to_string(wideId));
    }
    const int16_t rawId = static_cast<int16_t>(wideId);

    // Apply delta transform and write to output stream
    const uint16_t rawIdDelta = (uint16_t)(rawId) - (uint16_t)(prevId);
    writeFieldDelta(rawIdDelta);

    // Reject non-canonical Thrift
    if (rawIdDelta >= 1 && rawIdDelta <= 15 && deltaNibble == 0) {
        throw ThriftUnhandledInputError{
            "Invalid field header: delta is small but varint is present"
        };
    }
    const auto id = folly::to<ThriftNodeId>(rawId);

    auto field_it = struct_it.child(id, type);

    // TType::T_BOOL is a special case, the boolean value is bit-packed
    if (type == TType::T_BOOL) {
        const uint8_t val = parseBool(typeNibble);
        writeValue(field_it, val);
    }

    debug("Field header: type {}, id {}", type, rawId);
    return field_it;
}

ZL_FORCE_INLINE_ATTR DCompactParser::PT::Iterator
DCompactParser::unparseFieldHeader(
        const DCompactParser::PT::Iterator& struct_it,
        int16_t prevId)
{
    // Get the type
    const auto type    = readType();
    uint8_t typeNibble = unparseType(type);

    // TType::T_STOP is a special case, there is no field id
    if (type == TType::T_STOP) {
        writeValue<uint8_t>(0);
        return struct_it.stop();
    }

    // Get the field id
    const uint16_t rawIdDelta = readFieldDelta();
    const int16_t rawId       = (int16_t)(rawIdDelta + (uint16_t)(prevId));
    const auto id             = ThriftNodeId(rawId);

    auto field_it = struct_it.child(id, type);

    // TType::T_BOOL is a special case, the boolean value is bit-packed
    if (type == TType::T_BOOL) {
        const auto val = readValue<uint8_t>(field_it);
        typeNibble     = unparseBool(val);
    }

    // Construct TCompact field header
    const bool useVarint      = rawIdDelta < 1 || rawIdDelta > 15;
    const uint8_t deltaNibble = useVarint ? 0 : (uint8_t)rawIdDelta;
    assert(deltaNibble <= 15); // by construction
    assert(typeNibble <= 15);  // thanks to CTypeToTType bounds-check
    const uint8_t byte = (uint8_t)(deltaNibble << 4) | typeNibble;
    writeValue<uint8_t>(byte);
    if (useVarint) {
        writeValue<int16_t>(rawId);
    }

    debug("Field header: type {}, id {}", type, rawId);
    return field_it;
}

void CompactParser::advance(const CompactParser::PT::Iterator& current)
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
        case TType::T_BOOL:
            // Bools only get an explicit representation in compact protocol if
            // they're a top-level member of a collection.
            if (id == ThriftNodeId::kMapKey || id == ThriftNodeId::kMapValue
                || id == ThriftNodeId::kListElem) {
                const auto val = readValue<uint8_t>();
                writeValue(current, parseBool(val));
            }
            break;
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

bool DCompactParser::advanceIfTrivial(
        const DCompactParser::PT::Iterator& current)
{
    const auto type = current.type();
    const auto id   = current.id();
    debug("AdvanceIfTrivial: pos {}, path {}, type {} ({}), id {}",
          ws_.nbytes(),
          current.pathStr(),
          thriftTypeToString(type),
          type,
          (int32_t)id);
    switch (type) {
        case TType::T_BOOL:
            // Bools only get an explicit representation in compact protocol if
            // they're a top-level member of a collection.
            if (id == ThriftNodeId::kMapKey || id == ThriftNodeId::kMapValue
                || id == ThriftNodeId::kListElem) {
                const auto val = readValue<uint8_t>(current);
                writeValue(unparseBool(val));
            }
            return true;
        case TType::T_BYTE: {
            const auto val = readValue<int8_t>(current);
            writeValue(val);
            return true;
        }
        case TType::T_I16: {
            const auto val = readValue<int16_t>(current);
            writeValue(val);
            return true;
        }
        case TType::T_I32: {
            const auto val = readValue<int32_t>(current);
            writeValue(val);
            return true;
        }
        case TType::T_I64: {
            const auto val = readValue<int64_t>(current);
            writeValue(val);
            return true;
        }
        case TType::T_FLOAT: {
            const auto val = readValue<float>(current);
            writeValue(val);
            return true;
        }
        case TType::T_DOUBLE: {
            const auto val = readValue<double>(current);
            writeValue(val);
            return true;
        }
        case TType::T_STRING: {
            const auto len = readValue<uint32_t>(current.lengths());
            writeValue(len);
            copyBytes(current.stream(), len);
            return true;
        }
        case TType::T_MAP:
        case TType::T_SET:
        case TType::T_LIST:
        case TType::T_STRUCT:
            return false;
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

void DCompactParser::advance(const DCompactParser::PT::Iterator& current)
{
    if (advanceIfTrivial(current)) {
        return;
    }
    const auto type = current.type();
    const auto id   = current.id();
    debug("AdvanceNonTrivial: pos {}, path {}, type {} ({}), id {}",
          ws_.nbytes(),
          current.pathStr(),
          thriftTypeToString(type),
          type,
          (int32_t)id);
    switch (type) {
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
                if (!advanceIfTrivial(it)) {
                    advance(it);
                }
                prevId = int16_t(it.id());
            }
            break;
        }
        case TType::T_BOOL:
        case TType::T_BYTE:
        case TType::T_I16:
        case TType::T_I32:
        case TType::T_I64:
        case TType::T_FLOAT:
        case TType::T_DOUBLE:
        case TType::T_STRING:
        case TType::T_STOP:
        case TType::T_VOID:
        case TType::T_U16:
        case TType::T_U32:
        case TType::T_U64:
        case TType::T_UTF8:
        case TType::T_UTF16:
        case TType::T_STREAM:
        default:
            // advanceIfTrivial() already verified that we aren't in any of
            // these cases.
            assert(false);
            break;
    }
}

void CompactParser::parseTulipV2Header(const PT::Iterator& current)
{
    const PT::Iterator& it =
            current.child(ThriftNodeId::kMessageHeader, TType::T_STRING);

    uint8_t byte0;
    size_t headerSize = 0;
    byte0             = rs_.readValue<uint8_t>();
    writeValue(it, byte0);
    headerSize++;

    // Allow newline separators
    if (byte0 == uint8_t('\n')) {
        // Disallow newline following the final Thrift message
        byte0 = rs_.readValue<uint8_t>();
        writeValue(it, byte0);
        headerSize++;
    }

    const auto byte1 = rs_.readValue<uint8_t>();
    writeValue(it, byte1);
    headerSize++;

    if (byte0 != uint8_t(0x80) || byte1 != uint8_t(0x00)) {
        throw ThriftMalformedInputError("Bad TulipV2 header");
    }

    writeValue<uint32_t>(it.lengths(), folly::to<uint32_t>(headerSize));
}

} // namespace zstrong::thrift
