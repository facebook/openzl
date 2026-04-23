// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "custom_transforms/thrift/constants.h" // @manual
#include "custom_transforms/thrift/debug.h"
#include "custom_transforms/thrift/parse_config.h"  // @manual
#include "custom_transforms/thrift/thrift_errors.h" // @manual
#include "custom_transforms/thrift/thrift_types.h"  // @manual
#include "openzl/codecs/common/copy.h"              // @manual
#include "openzl/common/errors_internal.h"          // @manual
#include "openzl/shared/varint.h"                   // @manual

#include <folly/Portability.h>
#include <folly/Range.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBufQueue.h>
#include <folly/lang/Bits.h>
#include <cstdint>
#include <cstring>
#include <map>
#include <optional>
#include <variant>
#include <vector>

namespace zstrong::thrift {

size_t constexpr kFastCopyBytes = 32;

class ReadStream {
   public:
    explicit ReadStream(folly::ByteRange buf) : buf_(buf), begin_(buf.begin())
    {
    }

    template <typename T>
    explicit ReadStream(folly::Range<const T*> typedBuf)
    {
        folly::ByteRange rawBuf{ (const uint8_t*)typedBuf.data(),
                                 sizeof(T) * typedBuf.size() };
        buf_   = rawBuf;
        begin_ = rawBuf.start();
    }

    ZL_FORCE_INLINE_ATTR bool fast() const
    {
        return buf_.size() >= kFastCopyBytes;
    }

    ZL_FORCE_INLINE_ATTR folly::ByteRange readBytes(size_t n)
    {
        if (n > buf_.size()) {
            throw std::runtime_error{
                "Attempting to read past end-of-stream!"
            };
        }
        const folly::ByteRange result = { buf_.start(), n };
        buf_                          = { result.end(), buf_.end() };
        return result;
    }

    template <typename Value>
    ZL_FORCE_INLINE_ATTR Value readValue()
    {
        static_assert(folly::kIsLittleEndian);
        Value val;
        const folly::ByteRange bytes = readBytes(sizeof(Value));
        std::memcpy(&val, bytes.start(), bytes.size());
        return val;
    }

    ZL_FORCE_INLINE_ATTR uint64_t readVarint()
    {
        const uint8_t* ptr = buf_.data();
        auto const report  = ZL_varintDecode64Strict(&ptr, buf_.end());
        if (ZL_RES_isError(report)) {
            throw std::runtime_error("Failed to decode varint!");
        }
        assert(ptr <= buf_.end());
        buf_ = { ptr, buf_.end() };
        return ZL_RES_value(report);
    }

    size_t pos() const
    {
        return buf_.start() - begin_;
    }

    size_t nbytes() const
    {
        return buf_.end() - begin_;
    }

    std::string repr() const;

   private:
    folly::ByteRange buf_;
    const uint8_t* begin_;
};

namespace detail {

template <typename Derived>
class BaseWriteStream {
    Derived& derived()
    {
        return *static_cast<Derived*>(this);
    }

    Derived const& derived() const
    {
        return *static_cast<Derived const*>(this);
    }

   public:
    /**
     * Need to implement these functions
     */
    // size_t nbytes() const;
    // size_t width() const
    // void reserve(size_t n);
    // uint8_t* wptr();
    // void commit(size_t n);
    // std::vector<uint8_t> asVec() const;

    ZL_FORCE_INLINE_ATTR void writeBytes(folly::ByteRange bytes)
    {
        derived().reserve(bytes.size());
        std::copy(bytes.begin(), bytes.end(), derived().wptr());
        derived().commit(bytes.size());
    }

    ZL_FORCE_INLINE_ATTR void copyBytes(ReadStream& stream, size_t n)
    {
        if (stream.fast() && n <= kFastCopyBytes) {
            auto bytes = stream.readBytes(n);
            // The ZSDecodeWriteStream reserves kFastCopyBytes extra in the
            // output stream so that it won't fail this reserve due to being too
            // close to the end of the buffer.
            derived().reserve(kFastCopyBytes);

            memcpy(derived().wptr(), bytes.begin(), kFastCopyBytes);

            derived().commit(n);
        } else {
            writeBytes(stream.readBytes(n));
        }
    }

    template <typename Value>
    ZL_FORCE_INLINE_ATTR void writeValue(Value val)
    {
        static_assert(folly::kIsLittleEndian);
        derived().writeBytes({ (const uint8_t*)(&val), sizeof(Value) });
    }

    template <typename UInt>
    ZL_FORCE_INLINE_ATTR void writeVarint(UInt val)
    {
        static_assert(std::is_integral_v<UInt> && !std::is_signed_v<UInt>);
        assert(derived().width() == 1);
        if constexpr (sizeof(UInt) > 4) {
            derived().reserve(ZL_VARINT_FAST_OVERWRITE_64);
            size_t const commitSize =
                    ZL_varintEncode64Fast(val, derived().wptr());
            assert(commitSize <= ZL_VARINT_LENGTH_64);
            derived().commit(commitSize);
        } else {
            derived().reserve(ZL_VARINT_FAST_OVERWRITE_32);
            size_t const commitSize =
                    ZL_varintEncode32Fast(val, derived().wptr());
            assert(commitSize <= ZL_VARINT_LENGTH_32);
            derived().commit(commitSize);
        }
    }

    std::string repr() const
    {
        std::vector<uint8_t> const vec = derived().asVec();
        return bytesAsHex(folly::ByteRange{ vec.data(), vec.size() });
    }

    bool operator==(const BaseWriteStream& other) const
    {
        debug("Testing equality:");
        debug(derived().repr());
        debug(other.derived().repr());
        debug("\n");
        return derived().asVec() == other.derived().asVec();
    }
};

} // namespace detail

class WriteStream : public detail::BaseWriteStream<WriteStream> {
    using Base = detail::BaseWriteStream<WriteStream>;

    // Difficult to tune in a micro-benchmark
    static constexpr size_t kQueueAppenderGrowth = 128 * 1024;

   public:
    explicit WriteStream(TType type)
            : queue_(folly::IOBufQueue::cacheChainLength()),
              appender_(&queue_, kQueueAppenderGrowth),
              type_(type)
    {
        if (width() == 0) {
            // Note: It is not possible to create an EncoderConfig that throws
            // here with the provided construction API.
            throw InvalidConfigError{ fmt::format(
                    "Invalid WriteStream type {}", type_) };
        }
    }

    WriteStream(WriteStream&& other) noexcept
            : queue_(std::move(other.queue_)),
              appender_(&queue_, kQueueAppenderGrowth),
              type_(other.type_)
    {
    }

    WriteStream& operator=(WriteStream&& other) noexcept
    {
        queue_ = std::move(other.queue_);
        appender_.reset(&queue_, kQueueAppenderGrowth);
        type_ = other.type_;
        return *this;
    }

    // For unit tests
    template <typename T>
    WriteStream(TType type, std::vector<T> const& vec) : WriteStream(type)
    {
        folly::ByteRange const bytes{ (uint8_t const*)vec.data(),
                                      vec.size() * sizeof(T) };
        writeBytes(bytes);
    }

    // Overrides BaseWriteStream<WriteStream>::writeBytes()
    ZL_FORCE_INLINE_ATTR void writeBytes(folly::ByteRange bytes)
    {
        appender_.push(bytes);
    }

    // Overrides BaseWriteStream<WriteStream>::writeValue()
    template <typename Value>
    ZL_FORCE_INLINE_ATTR void writeValue(Value val)
    {
        static_assert(folly::kIsLittleEndian);
        appender_.writeLE<Value>(val);
    }

    void copyTo(folly::MutableByteRange dst) const
    {
        if (dst.size() != nbytes()) {
            throw std::runtime_error{
                "Failure in WriteStream::copyTo() : dst size must exactly match nbytes()"
            };
        }

        if (nbytes() > 0) {
            folly::io::Cursor cursor(queue_.front());
            uint8_t* dstPtr = dst.begin();
            while (!cursor.isAtEnd()) {
                folly::ByteRange const bytes = cursor.peekBytes();
                std::copy(bytes.begin(), bytes.end(), dstPtr);
                dstPtr += bytes.size();
                cursor += bytes.size();
            }
            assert(dstPtr == dst.end());
        }
    }

    size_t nbytes() const
    {
        return queue_.chainLength();
    }

    /// Reserve space to write up to @p n more bytes
    void reserve(size_t n)
    {
        appender_.ensure(n);
    }

    /// Reserve space to write up to @p n more bytes
    /// Enforce total size after reserve is <= max
    void reserve(size_t n, size_t max)
    {
        size_t const newSize = queue_.chainLength() + n;
        if (newSize > max) {
            throw std::runtime_error{
                "Requested reserve() exceeds maximum allowed size!"
            };
        }
        reserve(n);
    }

    // Note: invalidated by reserve(), commit(), and all write.*() methods
    uint8_t* wptr()
    {
        return appender_.writableData();
    }

    void commit(size_t n)
    {
        appender_.append(n);
    }

    size_t width() const
    {
        auto const w = getTypeInfo(type_, ZL_MAX_FORMAT_VERSION).width;
        assert(w == getTypeInfo(type_, ZL_MIN_FORMAT_VERSION).width);
        return w;
    }

    void setWidth(size_t w)
    {
        assert(w == width());
    }

    // Any writes after calling close() have undefined behavior
    void close()
    {
        assert(width() != 0);
    }

    std::vector<uint8_t> asVec() const
    {
        std::vector<uint8_t> vec(nbytes());
        folly::MutableByteRange range = { vec.data(), vec.size() };
        copyTo(range);
        return vec;
    }

    bool operator==(const WriteStream& other) const
    {
        const bool baseEqual = Base::operator==(other);
        return baseEqual && type_ == other.type_;
    }

    TType type() const
    {
        return type_;
    }

   private:
    folly::IOBufQueue queue_;
    folly::io::QueueAppender appender_;
    TType type_;
};

class FixedWriteStream : public detail::BaseWriteStream<FixedWriteStream> {
   public:
    FixedWriteStream() {}

    explicit FixedWriteStream(folly::MutableByteRange buffer)
            : ptr_(buffer.begin()), end_(buffer.end()), begin_(buffer.begin())
    {
    }

    size_t width() const
    {
        return 1;
    }

    size_t nbytes() const
    {
        assert(begin_ <= ptr_);
        assert(ptr_ <= end_);
        return size_t(ptr_ - begin_);
    }

    void reserve(size_t n)
    {
        assert(ptr_ <= end_);
        if (n > size_t(end_ - ptr_)) {
            throw std::runtime_error{ "Not enough space in buffer" };
        }
    }

    // Note: invalidated by commit() and all write.*() methods
    uint8_t* wptr()
    {
        return ptr_;
    }

    void commit(size_t n)
    {
        assert(ptr_ <= end_);
        assert(n <= size_t(end_ - ptr_));
        ptr_ += n;
    }

    std::vector<uint8_t> asVec() const
    {
        folly::ByteRange written = { begin_, nbytes() };
        return std::vector<uint8_t>(written.begin(), written.end());
    }

   private:
    uint8_t* ptr_;
    uint8_t* end_;
    uint8_t* begin_;
};

class ZSDecodeWriteStream {
   public:
    ZSDecodeWriteStream(ZL_Decoder* dictx, size_t originalSize)
    {
        // Reserve extra space to guarantee that we have room to overcopy
        size_t const streamSize = originalSize + kFastCopyBytes;
        stream_ = ZL_Decoder_create1OutStream(dictx, streamSize, 1);
        if (stream_ == nullptr) {
            throw std::bad_alloc{};
        }
        ws_ = FixedWriteStream{ { (uint8_t*)ZL_Output_ptr(stream_),
                                  streamSize } };
    }

    FixedWriteStream& writeStream()
    {
        return ws_;
    }

    ZL_Report commit()
    {
        return ZL_Output_commit(stream_, ws_.nbytes());
    }

   private:
    ZL_Output* stream_;
    FixedWriteStream ws_;
};

class WriteStreamSet {
   public:
    using StreamType = WriteStream;

    explicit WriteStreamSet(
            const BaseConfig& config,
            unsigned int formatVersion);

    WriteStream& getStream(SingletonId streamId)
    {
        return singletonStreams_.at(streamId);
    }

    const WriteStream& getStream(SingletonId streamId) const
    {
        return singletonStreams_.at(streamId);
    }

    WriteStream& getStream(LogicalId streamId)
    {
        return variableStreams_.at(streamId);
    }

    const WriteStream& getStream(LogicalId streamId) const
    {
        return variableStreams_.at(streamId);
    }

    WriteStream& getStringLengthStream(LogicalId streamId)
    {
        return variableStringLengthStreams_.at(streamId);
    }

    const WriteStream& getStringLengthStream(LogicalId streamId) const
    {
        return variableStringLengthStreams_.at(streamId);
    }

    std::string repr() const;

    // Helpers for unit tests:
    WriteStreamSet(
            folly::F14FastMap<SingletonId, WriteStream>&& singletonStreams,
            folly::F14FastMap<LogicalId, WriteStream>&& variableStreams)
            : singletonStreams_(std::move(singletonStreams)),
              variableStreams_(std::move(variableStreams))
    {
    }

    bool operator==(const WriteStreamSet& other) const
    {
        return (singletonStreams_ == other.singletonStreams_
                && variableStreams_ == other.variableStreams_);
    }

    const folly::F14FastMap<SingletonId, WriteStream>& getSingletonStreams()
            const
    {
        return singletonStreams_;
    }

    const folly::F14FastMap<LogicalId, WriteStream>& getVariableStreams() const
    {
        return variableStreams_;
    }

    const folly::F14FastMap<LogicalId, WriteStream>&
    getVariableStringLengthStreams() const
    {
        return variableStringLengthStreams_;
    }

    void closeAllStreams()
    {
        for (auto& [_, stream] : singletonStreams_) {
            stream.close();
        }
        for (auto& [_, stream] : variableStreams_) {
            stream.close();
        }
    }

   private:
    // Note: to avoid invalidating references, do not insert elements
    // outside of the WriteStreamSet constructor.
    folly::F14FastMap<SingletonId, WriteStream> singletonStreams_;
    folly::F14FastMap<LogicalId, WriteStream> variableStreams_;
    folly::F14FastMap<LogicalId, WriteStream> variableStringLengthStreams_;
};

class ReadStreamSet {
   public:
    using StreamType = ReadStream;

    ReadStreamSet(
            const DecoderConfig& config,
            const ZL_Input* compulsorySrcs[],
            size_t nbCompulsorySrcs,
            const ZL_Input* variableSrcs[],
            size_t nbVariableSrcs,
            unsigned int formatVersion);

    ReadStream& getStream(SingletonId streamId)
    {
        return singletonStreams_.at(streamId);
    }

    const ReadStream& getStream(SingletonId streamId) const
    {
        return singletonStreams_.at(streamId);
    }

    ReadStream& getStream(LogicalId streamId)
    {
        return variableStreams_.at(streamId);
    }

    const ReadStream& getStream(LogicalId streamId) const
    {
        return variableStreams_.at(streamId);
    }

    ReadStream& getStringLengthStream(LogicalId streamId)
    {
        return variableStringLengthStreams_.at(streamId);
    }

    const ReadStream& getStringLengthStream(LogicalId streamId) const
    {
        return variableStringLengthStreams_.at(streamId);
    }

    size_t pos() const;
    size_t nbytes() const;
    std::string repr() const;

   private:
    // Helpers for parsing streams from the Zstrong frame
    void consumeFixedWidthCluster(
            const ZL_Input* zStream,
            const LogicalCluster& clusterInfo,
            ReadStream& lengths,
            unsigned int formatVersion);
    void consumeStringCluster(
            const ZL_Input* zStream,
            const LogicalCluster& clusterInfo,
            ReadStream& lengths);

    // Note: to avoid invalidating references, do not insert elements
    // outside of the ReadStreamSet constructor.
    folly::F14FastMap<SingletonId, ReadStream> singletonStreams_;
    folly::F14FastMap<LogicalId, ReadStream> variableStreams_;
    folly::F14FastMap<LogicalId, ReadStream> variableStringLengthStreams_;
};

} // namespace zstrong::thrift
