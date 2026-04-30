// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "custom_transforms/thrift/parse_config.h"
#include "custom_transforms/thrift/path_tracker.h"
#include "custom_transforms/thrift/split_helpers.h"
#include "custom_transforms/thrift/thrift_errors.h"
#include "openzl/zl_errors.h"

#include <type_traits>

namespace zstrong::thrift {

struct ListInfo {
    size_t size;
    TType elemType;
};

struct MapInfo {
    size_t size;
    TType keyType;
    TType valueType;
};

template <typename Derived>
class BaseParser {
   protected:
    using PT = PathTracker<WriteStreamSet>;

    Derived& derived()
    {
        return static_cast<Derived&>(*this);
    }

   public:
    BaseParser(
            const EncoderConfig& config,
            ReadStream& src,
            WriteStreamSet& dsts,
            unsigned int formatVersion)
            : rs_(src),
              wss_(dsts),
              typeStream_(wss_.getStream(SingletonId::kTypes)),
              fieldDeltaStream_(wss_.getStream(SingletonId::kFieldDeltas)),
              tracker_(config, wss_, formatVersion),
              config_(config)
    {
    }

    void parse()
    {
        while (1) {
            // TODO(T193417465) Plug in external header parser here

            try {
                if (config_.getShouldParseTulipV2()) {
                    derived().parseTulipV2Header(tracker_.root());
                }
                derived().advance(tracker_.root());
                if (rs_.pos() == rs_.nbytes()) {
                    // Note: parser.advance() always makes forward progress
                    break;
                }
            } catch (const ThriftParserUserError& ex) {
                throw ThriftParserUserError{ fmt::format(
                        "Thrift parser failed at position {}: {}",
                        rs_.pos(),
                        ex.what()) };
            } catch (const std::exception& ex) {
                throw std::runtime_error{ fmt::format(
                        "Thrift parser failed at position {}: {}",
                        rs_.pos(),
                        ex.what()) };
            }

            // TODO(T193417465) Plug in external footer parser here
        }
    }

   protected:
    ReadStream& rs_;
    WriteStreamSet& wss_;
    WriteStream& typeStream_;
    WriteStream& fieldDeltaStream_;
    PT tracker_;
    const EncoderConfig& config_;

    // Consume at least one byte from rs_
    // Derived class must implement this function
    // void advance(const PT::Iterator& current);

    // Only called if config_.getShouldParseTulipV2() is true
    // Derived class must implement this function
    // void parseTulipV2Header(const PT::Iterator& current);

    // Protocol-independent read/write methods. Derived classes must declare and
    // implement any read/write methods not declared by the base class.
    ZL_FORCE_INLINE_ATTR folly::ByteRange readBytes(size_t n)
    {
        return rs_.readBytes(n);
    }

    ZL_FORCE_INLINE_ATTR void writeBytes(
            const PT::Iterator& it,
            folly::ByteRange bytes)
    {
        auto& ws = it.stream();
        ws.setWidth(1);
        ws.writeBytes(bytes);
    }

    template <typename Value>
    ZL_FORCE_INLINE_ATTR void writeValue(const PT::Iterator& it, Value val)
    {
        auto& ws = it.stream();
        ws.setWidth(sizeof(Value));
        ws.writeValue(val);
    }

    ZL_FORCE_INLINE_ATTR void writeType(TType type)
    {
        typeStream_.setWidth(1);
        typeStream_.writeValue((uint8_t)type);
    }

    ZL_FORCE_INLINE_ATTR void writeFieldDelta(int16_t delta)
    {
        fieldDeltaStream_.setWidth(2);
        fieldDeltaStream_.writeValue(delta);
    }

    template <typename Value>
    ZL_FORCE_INLINE_ATTR void parsePrimitiveListBody(
            WriteStream& ws,
            size_t numElts)
    {
        ws.setWidth(sizeof(Value));
        ws.reserve(numElts * sizeof(Value), rs_.nbytes() * sizeof(Value));
        for (size_t i = 0; i < numElts; ++i) {
            const Value val = derived().template readValue<Value>();
            // TODO(T193417685) Exploit the fact that we reserve() up front to
            // avoid bounds check in writeValue()
            ws.writeValue(val);
        }
    }

    void parseList(const PT::Iterator& current)
    {
        const ListInfo info = derived().parseListHeader(current);
        const auto elemIt   = current.listElem(info.elemType);

        switch (info.elemType) {
            case TType::T_I16: {
                auto& ws = elemIt.stream();
                parsePrimitiveListBody<int16_t>(ws, info.size);
                break;
            }
            case TType::T_I32: {
                auto& ws = elemIt.stream();
                parsePrimitiveListBody<int32_t>(ws, info.size);
                break;
            }
            case TType::T_I64: {
                auto& ws = elemIt.stream();
                parsePrimitiveListBody<int64_t>(ws, info.size);
                break;
            }
            case TType::T_FLOAT: {
                auto& ws = elemIt.stream();
                parsePrimitiveListBody<float>(ws, info.size);
                break;
            }
            case TType::T_DOUBLE: {
                auto& ws = elemIt.stream();
                parsePrimitiveListBody<double>(ws, info.size);
                break;
            }
            default: {
                for (size_t i = 0; i < info.size; i++) {
                    derived().advance(elemIt);
                }
            }
        }
    }

    ZL_FORCE_INLINE_ATTR void parseMapFallback(
            const PT::Iterator& current,
            const MapInfo info)
    {
        assert(info.size > 0);
        const auto keyIt   = current.mapKey(info.keyType);
        const auto valueIt = current.mapValue(info.valueType);
        for (size_t i = 0; i < info.size; i++) {
            derived().advance(keyIt);
            derived().advance(valueIt);
        }
    }

    template <typename Key, typename Value>
    ZL_FORCE_INLINE_ATTR void parsePrimitiveMapBody(
            WriteStream& keyWriteStream,
            WriteStream& valueWriteStream,
            size_t numElts)
    {
        keyWriteStream.setWidth(sizeof(Key));
        valueWriteStream.setWidth(sizeof(Value));
        keyWriteStream.reserve(
                numElts * sizeof(Key), rs_.nbytes() * sizeof(Key));
        valueWriteStream.reserve(
                numElts * sizeof(Value), rs_.nbytes() * sizeof(Value));
        for (size_t i = 0; i < numElts; ++i) {
            const Key key   = derived().template readValue<Key>();
            const Value val = derived().template readValue<Value>();
            // TODO(T193417685) Exploit the fact that we reserve() up front to
            // avoid bounds check in writeValue()
            keyWriteStream.writeValue(key);
            valueWriteStream.writeValue(val);
        }
    }

    template <typename Key>
    ZL_FORCE_INLINE_ATTR void parseMapHelper(
            const PT::Iterator& current,
            const MapInfo info)
    {
        assert(info.size > 0);
        const auto keyIt   = current.mapKey(info.keyType);
        const auto valueIt = current.mapValue(info.valueType);

        switch (info.valueType) {
            case TType::T_I32: {
                auto& keyWriteStream   = keyIt.stream();
                auto& valueWriteStream = valueIt.stream();
                parsePrimitiveMapBody<Key, int32_t>(
                        keyWriteStream, valueWriteStream, info.size);
                break;
            }
            case TType::T_I64: {
                auto& keyWriteStream   = keyIt.stream();
                auto& valueWriteStream = valueIt.stream();
                parsePrimitiveMapBody<Key, int64_t>(
                        keyWriteStream, valueWriteStream, info.size);
                break;
            }
            case TType::T_FLOAT: {
                auto& keyWriteStream   = keyIt.stream();
                auto& valueWriteStream = valueIt.stream();
                parsePrimitiveMapBody<Key, float>(
                        keyWriteStream, valueWriteStream, info.size);
                break;
            }
            case TType::T_DOUBLE: {
                auto& keyWriteStream   = keyIt.stream();
                auto& valueWriteStream = valueIt.stream();
                parsePrimitiveMapBody<Key, double>(
                        keyWriteStream, valueWriteStream, info.size);
                break;
            }
            default: {
                parseMapFallback(current, info);
            }
        }
    }

    void parseMap(const PT::Iterator& current)
    {
        const MapInfo info = derived().parseMapHeader(current);
        if (info.size > 0) {
            switch (info.keyType) {
                case TType::T_I32: {
                    parseMapHelper<int32_t>(current, info);
                    break;
                }
                case TType::T_I64: {
                    parseMapHelper<int64_t>(current, info);
                    break;
                }
                default: {
                    parseMapFallback(current, info);
                }
            }
        }
    }
};

template <typename Derived>
class DBaseParser {
   protected:
    using PT = PathTracker<ReadStreamSet>;

    Derived& derived()
    {
        return static_cast<Derived&>(*this);
    }

   public:
    DBaseParser(
            const DecoderConfig& config,
            ReadStreamSet& srcs,
            FixedWriteStream& dst,
            unsigned int formatVersion)
            : ws_(dst),
              rss_(srcs),
              typeStream_(rss_.getStream(SingletonId::kTypes)),
              fieldDeltaStream_(rss_.getStream(SingletonId::kFieldDeltas)),
              tracker_(config, rss_, formatVersion),
              config_(config)
    {
    }

    void unparse()
    {
        while (1) {
            // TODO(T193417465) Plug in external header parser here

            try {
                if (config_.getShouldUnparseMessageHeaders()) {
                    unparseMessageHeader(tracker_.root());
                }
                derived().advance(tracker_.root());
                if (ws_.nbytes() == config_.getOriginalSize()) {
                    // Note: parser.advance() always makes forward progress
                    break;
                }
                assert(ws_.nbytes() <= kMaxExpansionFactor * rss_.nbytes());
            } catch (const ThriftParserUserError& ex) {
                throw ThriftParserUserError{ fmt::format(
                        "Thrift parser failed at position {}: {}",
                        ws_.nbytes(),
                        ex.what()) };
            } catch (const std::exception& ex) {
                throw std::runtime_error{ fmt::format(
                        "Thrift parser failed at position {}: {}",
                        ws_.nbytes(),
                        ex.what()) };
            }

            // TODO(T193417465) Plug in external footer parser here
        }
    }

   protected:
    FixedWriteStream& ws_;
    ReadStreamSet& rss_;
    ReadStream& typeStream_;
    ReadStream& fieldDeltaStream_;
    PT tracker_;
    const DecoderConfig& config_;

    // Consume at least one byte from rss_
    // Derived class must implement this function
    // void advance(const PT::Iterator& current);

    void unparseMessageHeader(const PT::Iterator& current)
    {
        const PT::Iterator& it =
                current.child(ThriftNodeId::kMessageHeader, TType::T_STRING);
        const auto size = readValue<uint32_t>(it.lengths());
        copyBytes(it.stream(), size);
    }

    // Protocol-independent read/write methods. Derived classes must declare and
    // implement any read/write methods not declared by the base class.
    ZL_FORCE_INLINE_ATTR void copyBytes(ReadStream& rs, size_t n)
    {
        assert(ws_.width() == 1);
        ws_.copyBytes(rs, n);
    }

    template <typename Value>
    ZL_FORCE_INLINE_ATTR Value readValue(const PT::Iterator& it)
    {
        auto& rs = it.stream();
        return rs.readValue<Value>();
    }

    ZL_FORCE_INLINE_ATTR TType readType()
    {
        return typeStream_.readValue<TType>();
    }

    ZL_FORCE_INLINE_ATTR int16_t readFieldDelta()
    {
        return fieldDeltaStream_.readValue<int16_t>();
    }

    template <typename Value>
    struct UnparseValue {
       public:
        ZL_FORCE_INLINE_ATTR UnparseValue(
                DBaseParser* self,
                const PT::Iterator& current)
                : self_(*self), rs_(current.stream())
        {
        }

        ZL_FORCE_INLINE_ATTR void operator()()
        {
            auto const val = rs_.readValue<Value>();
            self_.derived().writeValue(val);
        }

       private:
        DBaseParser& self_;
        ReadStream& rs_;
    };

    template <>
    struct UnparseValue<StringType> {
       public:
        ZL_FORCE_INLINE_ATTR UnparseValue(
                DBaseParser* self,
                const PT::Iterator& current)
                : self_(*self),
                  contentStream_(current.stream()),
                  lengthStream_(current.lengths().stream())
        {
        }

        ZL_FORCE_INLINE_ATTR void operator()()
        {
            auto const length = lengthStream_.readValue<uint32_t>();
            self_.derived().writeValue(length);
            self_.derived().copyBytes(contentStream_, length);
        }

       private:
        DBaseParser& self_;
        ReadStream& contentStream_;
        ReadStream& lengthStream_;
    };

    template <>
    struct UnparseValue<AnyType> {
       public:
        ZL_FORCE_INLINE_ATTR UnparseValue(
                DBaseParser* self,
                const PT::Iterator& current)
                : self_(*self), it_(current)
        {
        }

        ZL_FORCE_INLINE_ATTR void operator()()
        {
            self_.derived().advance(it_);
        }

       private:
        DBaseParser& self_;
        const PT::Iterator& it_;
    };

    template <typename Value>
    void unparsePrimitiveListBody(const PT::Iterator& elemIt, size_t numElts)
    {
        UnparseValue<Value> unparseValue(this, elemIt);
        // Skip reserve() because ws_ is a FixedWriteStream
        static_assert(std::is_same_v<decltype(ws_), FixedWriteStream&>);
        for (size_t i = 0; i < numElts; ++i) {
            unparseValue();
        }
    }

    ZL_FORCE_INLINE_ATTR void unparseList(const PT::Iterator& current)
    {
        const ListInfo info = derived().unparseListHeader(current);
        if (info.size > 0) {
            const auto elemIt = current.listElem(info.elemType);
            switch (info.elemType) {
                case TType::T_I16:
                    unparsePrimitiveListBody<int16_t>(elemIt, info.size);
                    break;
                case TType::T_I32:
                    unparsePrimitiveListBody<int32_t>(elemIt, info.size);
                    break;
                case TType::T_I64:
                    unparsePrimitiveListBody<int64_t>(elemIt, info.size);
                    break;
                case TType::T_FLOAT:
                    unparsePrimitiveListBody<float>(elemIt, info.size);
                    break;
                case TType::T_DOUBLE:
                    unparsePrimitiveListBody<double>(elemIt, info.size);
                    break;
                case TType::T_STRING:
                    unparsePrimitiveListBody<StringType>(elemIt, info.size);
                    break;
                default:
                    unparsePrimitiveListBody<AnyType>(elemIt, info.size);
                    break;
            }
        }
    }

    template <typename Key, typename Value>
    void unparsePrimitiveMapBody(
            const PT::Iterator& keyIt,
            const PT::Iterator& valueIt,
            size_t numElts)
    {
        UnparseValue<Key> unparseKey(this, keyIt);
        UnparseValue<Value> unparseValue(this, valueIt);
        // Skip reserve() because ws_ is a FixedWriteStream
        static_assert(std::is_same_v<decltype(ws_), FixedWriteStream&>);
        for (size_t i = 0; i < numElts; ++i) {
            unparseKey();
            unparseValue();
        }
    }

    template <typename Key>
    ZL_FORCE_INLINE_ATTR void unparseMapHelper(
            const PT::Iterator& keyIt,
            const PT::Iterator& valueIt,
            const MapInfo info)
    {
        assert(info.size > 0);
        switch (info.valueType) {
            case TType::T_I32:
                unparsePrimitiveMapBody<Key, int32_t>(
                        keyIt, valueIt, info.size);
                break;
            case TType::T_I64:
                unparsePrimitiveMapBody<Key, int64_t>(
                        keyIt, valueIt, info.size);
                break;
            case TType::T_FLOAT:
                unparsePrimitiveMapBody<Key, float>(keyIt, valueIt, info.size);
                break;
            case TType::T_DOUBLE:
                unparsePrimitiveMapBody<Key, double>(keyIt, valueIt, info.size);
                break;
            case TType::T_STRING:
                unparsePrimitiveMapBody<Key, StringType>(
                        keyIt, valueIt, info.size);
                break;
            default:
                unparsePrimitiveMapBody<Key, AnyType>(
                        keyIt, valueIt, info.size);
                break;
        }
    }

    ZL_FORCE_INLINE_ATTR void unparseMap(const PT::Iterator& current)
    {
        const MapInfo info = derived().unparseMapHeader(current);
        if (info.size > 0) {
            const auto keyIt   = current.mapKey(info.keyType);
            const auto valueIt = current.mapValue(info.valueType);

            switch (info.keyType) {
                case TType::T_I32:
                    unparseMapHelper<int32_t>(keyIt, valueIt, info);
                    break;
                case TType::T_I64:
                    unparseMapHelper<int64_t>(keyIt, valueIt, info);
                    break;
                case TType::T_STRING:
                    unparseMapHelper<StringType>(keyIt, valueIt, info);
                    break;
                default:
                    unparseMapHelper<AnyType>(keyIt, valueIt, info);
                    break;
            }
        }
    }
};

} // namespace zstrong::thrift
