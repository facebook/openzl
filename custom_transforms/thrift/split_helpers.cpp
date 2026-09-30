// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/thrift/split_helpers.h" // @manual
#include "custom_transforms/thrift/debug.h"         // @manual
#include "openzl/zl_data.h"

#include <stdexcept>

namespace zstrong::thrift {
namespace {
folly::ByteRange zStreamToByteRange(const ZL_Input* stream)
{
    const auto src    = (uint8_t const*)ZL_Input_ptr(stream);
    const size_t size = ZL_Input_contentSize(stream);
    return { src, size };
}
} // namespace

WriteStreamSet::WriteStreamSet(
        const BaseConfig& config,
        unsigned int formatVersion)
{
    // Construct singleton streams
    for (int i = 0; static_cast<SingletonId>(i) < SingletonId::kNumSingletonIds;
         i++) {
        singletonStreams_.emplace(
                static_cast<SingletonId>(i),
                WriteStream(SingletonIdToTType[i]));
    }

    // Construct variable streams
    for (const auto& [path, info] : config.pathMap()) {
        variableStreams_.emplace(info.id, WriteStream(info.type));
        if (info.type == TType::T_STRING
            && formatVersion >= kMinFormatVersionStringVSF) {
            variableStringLengthStreams_.emplace(
                    info.id, WriteStream(TType::T_U32));
        }
    }
}

void ReadStreamSet::consumeFixedWidthCluster(
        const ZL_Input* zStream,
        const LogicalCluster& clusterInfo,
        ReadStream& chunkLengths,
        unsigned int formatVersion)
{
    [[maybe_unused]] const auto type = ZL_Input_type(zStream);
    assert(type == ZL_Type_serial || type == ZL_Type_numeric);

    const uint8_t* rptr      = (const uint8_t*)ZL_Input_ptr(zStream);
    const uint8_t* const end = rptr + ZL_Input_contentSize(zStream);
    for (LogicalId id : clusterInfo.idList) {
        // Format version 14 changes the cluster lengths stream from
        // counting bytes to counting elements
        uint32_t numBytes;
        if (formatVersion < kMinFormatVersionStringVSF) {
            numBytes = chunkLengths.readValue<uint32_t>();
        } else {
            numBytes = chunkLengths.readValue<uint32_t>()
                    * ZL_Input_eltWidth(zStream);
        }
        if (numBytes > end - rptr) {
            throw std::runtime_error{
                "Corruption: segment length exceeds stream size"
            };
        }
        const folly::ByteRange range{ rptr, numBytes };
        variableStreams_.emplace(id, range);
        rptr += numBytes;
    }

    if (rptr != end) {
        throw std::runtime_error{ "Failed to consume stream!" };
    }
}

void ReadStreamSet::consumeStringCluster(
        const ZL_Input* zStream,
        const LogicalCluster& clusterInfo,
        ReadStream& chunkLengths)
{
    [[maybe_unused]] const auto type = ZL_Input_type(zStream);
    assert(type == ZL_Type_string);

    const uint8_t* contentPtr = (const uint8_t*)ZL_Input_ptr(zStream);
    const uint8_t* const contentEnd =
            contentPtr + ZL_Input_contentSize(zStream);
    const uint32_t* fieldSizesPtr = ZL_Input_stringLens(zStream);
    const uint32_t* const fieldSizesEnd =
            fieldSizesPtr + ZL_Input_numElts(zStream);

    for (LogicalId id : clusterInfo.idList) {
        // Copy field sizes
        const uint32_t numElts = chunkLengths.readValue<uint32_t>();
        if (numElts > fieldSizesEnd - fieldSizesPtr) {
            throw std::runtime_error{
                "Corruption: numElts overflows fieldSizes buffer"
            };
        }
        const folly::Range<const uint32_t*> fieldSizesRange{ fieldSizesPtr,
                                                             numElts };
        variableStringLengthStreams_.emplace(id, fieldSizesRange);
        fieldSizesPtr += numElts;

        // Copy content
        const uint64_t numBytes = std::accumulate(
                fieldSizesRange.start(), fieldSizesRange.end(), (uint64_t)0);
        if (numBytes > contentEnd - contentPtr) {
            throw std::runtime_error{
                "Corruption: numBytes overflows content buffer"
            };
        }
        const folly::ByteRange content{ contentPtr, numBytes };
        variableStreams_.emplace(id, content);
        contentPtr += numBytes;
    }

    if (contentPtr != contentEnd) {
        throw std::runtime_error{ "Failed to consume content stream!" };
    }
    if (fieldSizesPtr != fieldSizesEnd) {
        throw std::runtime_error{ "Failed to consume field sizes stream!" };
    }
}

ReadStreamSet::ReadStreamSet(
        const DecoderConfig& config,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs,
        unsigned int formatVersion)
{
    folly::Range<const ZL_Input**> compulsorySrcRange{ compulsorySrcs,
                                                       nbCompulsorySrcs };
    folly::Range<const ZL_Input**> variableSrcRange{ variableSrcs,
                                                     nbVariableSrcs };
    size_t compulsoryIdx = 0;
    size_t variableIdx   = 0;

    // We can upper-bound the number of streams in advance!
    singletonStreams_.reserve(nbCompulsorySrcs);
    variableStreams_.reserve(nbVariableSrcs);
    variableStringLengthStreams_.reserve(nbVariableSrcs);

    // Read singleton streams
    for (const ZL_Input* const zStream : compulsorySrcRange) {
        const folly::ByteRange range = zStreamToByteRange(zStream);
        singletonStreams_.emplace(SingletonId(compulsoryIdx++), range);
    }

    // Read unclustered variable streams
    for (LogicalId const id : config.getUnclusteredStreams()) {
        const ZL_Input* zStream      = variableSrcRange.at(variableIdx++);
        const folly::ByteRange range = zStreamToByteRange(zStream);
        variableStreams_.emplace(id, range);

        if (ZL_Input_type(zStream) == ZL_Type_string) {
            folly::Range<const uint32_t*> lengths{ ZL_Input_stringLens(zStream),
                                                   ZL_Input_numElts(zStream) };
            variableStringLengthStreams_.emplace(id, lengths);
        }
    }

    // Read clustered variable streams
    if (config.clusters().size() > 0) {
        // This arithmetic is ugly, but I don't see a clean way to avoid it
        const size_t chunkLengthStreamIdx =
                variableIdx + config.clusters().size();
        folly::ByteRange chunkLengthsRange =
                zStreamToByteRange(variableSrcRange.at(chunkLengthStreamIdx));
        ReadStream chunkLengths{ chunkLengthsRange };

        for (const LogicalCluster& cluster : config.clusters()) {
            const ZL_Input* zStream = variableSrcRange.at(variableIdx++);
            const auto type         = ZL_Input_type(zStream);
            switch (type) {
                case ZL_Type_string:
                    consumeStringCluster(zStream, cluster, chunkLengths);
                    break;
                case ZL_Type_serial:
                case ZL_Type_numeric:
                    consumeFixedWidthCluster(
                            zStream, cluster, chunkLengths, formatVersion);
                    break;
                default:
                    throw std::runtime_error{ fmt::format(
                            "Unexpected Zstrong stream type: {}",
                            fmt::underlying(type)) };
            }
        }

        // Consume the chunk length stream
        variableIdx++;
    }

    if (variableIdx != variableSrcRange.size()) {
        throw std::runtime_error{
            "Corruption: failed to consume all variable streams!"
        };
    }
}

std::string ReadStream::repr() const
{
    return bytesAsHex(begin_, nbytes());
}

std::string WriteStreamSet::repr() const
{
    std::stringstream ss;
    for (const auto& [id, stream] : singletonStreams_) {
        ss << fmt::format(
                "Singleton {}: {}", fmt::underlying(id), stream.repr())
           << std::endl;
    }
    for (const auto& [id, stream] : variableStreams_) {
        ss << fmt::format(
                "Variable Content {}: {}", fmt::underlying(id), stream.repr())
           << std::endl;
    }
    for (const auto& [id, stream] : variableStringLengthStreams_) {
        ss << fmt::format(
                "Variable Lengths {}: {}", fmt::underlying(id), stream.repr())
           << std::endl;
    }
    return ss.str();
}

std::string ReadStreamSet::repr() const
{
    std::stringstream ss;
    for (const auto& [_, stream] : singletonStreams_) {
        ss << stream.repr() << std::endl;
    }
    size_t idx = 0;
    for (const auto& [_, stream] : variableStreams_) {
        ss << stream.repr();
        if (idx != variableStreams_.size() - 1) {
            ss << std::endl;
        }
        idx += 1;
    }
    return ss.str();
}

// TODO(T193417849) Make this O(1) passing a callback into the ReadStream
// constructor so the ReadStreams can all update a single position counter. This
// is in the hot path for decompression.
size_t ReadStreamSet::pos() const
{
    size_t totalConsumed = 0;
    for (const auto& [_, stream] : singletonStreams_) {
        totalConsumed += stream.pos();
    }
    for (const auto& [_, stream] : variableStreams_) {
        totalConsumed += stream.pos();
    }
    return totalConsumed;
}

// TODO(T193417864) Make this O(1) amortized by memoization. This is in the hot
// path for decompression.
size_t ReadStreamSet::nbytes() const
{
    size_t totalSize = 0;
    for (const auto& [_, stream] : singletonStreams_) {
        totalSize += stream.nbytes();
    }
    for (const auto& [_, stream] : variableStreams_) {
        totalSize += stream.nbytes();
    }
    return totalSize;
}

} // namespace zstrong::thrift
