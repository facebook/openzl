// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/thrift/thrift_parsers.h"    // @manual
#include "custom_transforms/thrift/binary_splitter.h"   // @manual
#include "custom_transforms/thrift/compact_splitter.h"  // @manual
#include "custom_transforms/thrift/constants.h"         // @manual
#include "custom_transforms/thrift/debug.h"             // @manual
#include "custom_transforms/thrift/directed_selector.h" // @manual
#include "custom_transforms/thrift/parse_config.h"      // @manual
#include "custom_transforms/thrift/split_helpers.h"     // @manual
#include "custom_transforms/thrift/thrift_types.h"      // @manual

#include "folly/Range.h"
#include "folly/lang/Bits.h"
#include "openzl/common/errors_internal.h"
#include "openzl/common/logging.h"
#include "openzl/decompress/dictx.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_data.h"
#include "openzl/zl_dtransform.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace zstrong::thrift {
namespace {

ZL_Output* createZstrongStream(
        ZL_Encoder* eic,
        int outcomeIndex,
        size_t nbElts,
        size_t eltWidth)
{
    ZL_Output* const zs =
            ZL_Encoder_createTypedStream(eic, outcomeIndex, nbElts, eltWidth);
    if (zs == nullptr)
        throw std::bad_alloc();
    return zs;
}

void copyFixedWidthWriteStreamToZstrongStream(
        ZL_Output* zs,
        const WriteStream& ws,
        size_t dstCapacityBytes)
{
    assert(dstCapacityBytes >= ws.nbytes());
    if (ws.nbytes() > 0) {
        const auto zsPtr = (uint8_t*)ZL_Output_ptr(zs);
        folly::MutableByteRange zsRange{ zsPtr, dstCapacityBytes };
        ws.copyTo(zsRange);
    }
}

ZL_Output* copyFixedWidthWriteStreamToEICtx(
        ZL_Encoder* eictx,
        const WriteStream& ws,
        StreamId streamId)
{
    OutcomeInfo outcomeInfo;
    if (std::holds_alternative<SingletonId>(streamId)) {
        outcomeInfo = getOutcomeInfo(std::get<SingletonId>(streamId));
    } else {
        VariableOutcome outcome = ws.width() == 1 ? VariableOutcome::kSerialized
                                                  : VariableOutcome::kNumeric;
        outcomeInfo             = getOutcomeInfo(outcome);
    }

    // Guaranteed thanks to type homogeneity enforcement
    ZL_ASSERT(ws.nbytes() % ws.width() == 0);

    size_t const nbElts = ws.nbytes() / ws.width();
    ZL_ASSERT_LT(outcomeInfo.idx, INT_MAX);
    ZL_Output* const zStream = createZstrongStream(
            eictx, (int)outcomeInfo.idx, nbElts, ws.width());
    copyFixedWidthWriteStreamToZstrongStream(
            zStream, ws, ZL_validResult(ZL_Output_contentCapacity(zStream)));
    if (ZL_isError(ZL_Output_commit(zStream, nbElts))) {
        throw std::runtime_error{ "Failed to commit Zstrong stream" };
    }

    return zStream;
}

void sanityCheckStringLengthsDebugOnly(
        const WriteStream& lengthStream,
        const size_t declaredContentSize,
        const size_t declaredNbElts)
{
#ifndef NDEBUG
    // Sanity check field lengths
    const std::vector<uint8_t> lengthsVec = lengthStream.asVec();
    assert(lengthsVec.size() % sizeof(uint32_t) == 0);
    assert(lengthsVec.size() / sizeof(uint32_t) == declaredNbElts);
    folly::Range<const uint32_t*> lengthsRange{
        (const uint32_t*)lengthsVec.data(), declaredNbElts
    };
    const size_t impliedContentSize = std::accumulate(
            lengthsRange.begin(), lengthsRange.end(), (uint32_t)0);
    assert(declaredContentSize == impliedContentSize);
#endif
}

ZL_Output* copyStringWriteStreamToEICtx(
        ZL_Encoder* eictx,
        const WriteStream& ws,
        const WriteStream& lengths)
{
    OutcomeInfo outcomeInfo = getOutcomeInfo(VariableOutcome::kVSF);
    assert(ws.type() == TType::T_STRING);
    assert(lengths.type() == TType::T_U32);
    size_t const nbElts = lengths.nbytes() / sizeof(uint32_t);
    sanityCheckStringLengthsDebugOnly(lengths, ws.nbytes(), nbElts);

    ZL_Output* const zs = ZL_Encoder_createStringStream(
            eictx, outcomeInfo.idx, nbElts, ws.nbytes());
    if (zs == nullptr)
        throw std::bad_alloc();

    // Copy the field lengths stream
    if (nbElts > 0) {
        uint32_t* fieldSizesPtr = ZL_Output_stringLens(zs);
        folly::MutableByteRange fieldSizesRange{ (uint8_t*)fieldSizesPtr,
                                                 nbElts * sizeof(uint32_t) };
        lengths.copyTo(fieldSizesRange);
    }

    copyFixedWidthWriteStreamToZstrongStream(
            zs, ws, ZL_validResult(ZL_Output_contentCapacity(zs)));

    // For VSF streams, commit() is only possible after field lengths are copied
    if (ZL_isError(ZL_Output_commit(zs, nbElts))) {
        throw std::runtime_error{ "Failed to commit Zstrong stream" };
    }

    return zs;
}

size_t logicalClusterNbBytes(
        const LogicalCluster& cluster,
        const WriteStreamSet& wss)
{
    size_t nbBytes = 0;
    for (LogicalId id : cluster.idList) {
        const WriteStream& ws = wss.getStream(id);
        nbBytes += ws.nbytes();
    }
    return nbBytes;
}

TType getClusterType(const LogicalCluster& cluster, const WriteStreamSet& wss)
{
    if (cluster.idList.empty()) {
        throw std::runtime_error{ "Type of an empty cluster is not defined" };
    }

    TType type = TType::T_VOID;
    for (const LogicalId id : cluster.idList) {
        const WriteStream& ws = wss.getStream(id);
        if (type == TType::T_VOID) {
            assert(ws.type() != TType::T_VOID);
            type = ws.type();
        }
        if (ws.type() != type) {
            throw std::runtime_error{ fmt::format(
                    "Cluster mixes different stream types: {} and {}",
                    ws.type(),
                    type) };
        }
    }
    return type;
}

// Only works for VSF streams
size_t logicalClusterNbElts(
        const LogicalCluster& cluster,
        const WriteStreamSet& wss)
{
    assert(getClusterType(cluster, wss) == TType::T_STRING);
    size_t nbElts = 0;
    for (LogicalId id : cluster.idList) {
        const WriteStream& ws = wss.getStringLengthStream(id);
        assert(ws.nbytes() % sizeof(uint32_t) == 0);
        nbElts += ws.nbytes() / sizeof(uint32_t);
    }
    return nbElts;
}

ZL_Output* copyFixedWidthLogicalClusterToEICtx(
        ZL_Encoder* eictx,
        const LogicalCluster& cluster,
        const WriteStreamSet& wss,
        WriteStream& lengths,
        unsigned int formatVersion)
{
    // Get cluster metadata
    const size_t clusterWidth = cluster.idList.size() == 0
            ? 1
            : wss.getStream(cluster.idList[0]).width();
    assert(clusterWidth > 0);
    const VariableOutcome outcome = clusterWidth == 1
            ? VariableOutcome::kSerialized
            : VariableOutcome::kNumeric;
    const OutcomeInfo outcomeInfo = getOutcomeInfo(outcome);
    size_t const nbBytes          = logicalClusterNbBytes(cluster, wss);

    // Guaranteed by type homogeneity enforcement and ParseConfig validation
    assert(nbBytes % clusterWidth == 0);
    size_t const nbElts = nbBytes / clusterWidth;

    // Create a variable stream for the cluster
    ZL_ASSERT_LT(outcomeInfo.idx, INT_MAX);
    ZL_Output* const zStream = createZstrongStream(
            eictx, (int)outcomeInfo.idx, nbElts, clusterWidth);

    // Concatenate data into the variable stream
    uint8_t* zsPtr = (uint8_t*)ZL_Output_ptr(zStream);
    if (ZL_isError(ZL_Output_commit(zStream, nbElts))) {
        throw std::runtime_error{ "Failed to commit Zstrong stream" };
    }
    [[maybe_unused]] const uint8_t* const zsEnd =
            zsPtr + ZL_Data_contentSize(ZL_codemodOutputAsData(zStream));
    for (LogicalId id : cluster.idList) {
        const WriteStream& ws = wss.getStream(id);
        if (ws.nbytes() > 0) {
            assert(zsPtr + ws.nbytes() <= zsEnd);
            folly::MutableByteRange chunkRange{ zsPtr, ws.nbytes() };
            ws.copyTo(chunkRange);
            zsPtr += ws.nbytes();
            assert(ws.nbytes() % clusterWidth == 0);
        }

        // Format version 14 changes the cluster lengths stream from counting
        // bytes to counting elements
        if (formatVersion < kMinFormatVersionStringVSF) {
            lengths.writeValue<uint32_t>(ws.nbytes());
        } else {
            const uint32_t segmentSizeElts =
                    folly::to<uint32_t>(ws.nbytes()) / clusterWidth;
            lengths.writeValue<uint32_t>(segmentSizeElts);
            assert(segmentSizeElts * clusterWidth == ws.nbytes());
        }
    }

    // Attach successor metadata
    if (ZL_isError(ZL_Output_setIntMetadata(
                zStream, kDirectedSelectorMetadataID, cluster.successor))) {
        throw std::runtime_error{ "Failed to set directed selector metadata" };
    }

    return zStream;
}

// Can only be used on format version 14 and above
ZL_Output* copyStringLogicalClusterToEICtx(
        ZL_Encoder* eictx,
        const LogicalCluster& cluster,
        const WriteStreamSet& wss,
        WriteStream& clusterLengths)
{
    // Get cluster metadata
    const OutcomeInfo outcomeInfo = getOutcomeInfo(VariableOutcome::kVSF);
    size_t const nbBytes          = logicalClusterNbBytes(cluster, wss);
    size_t const nbElts           = logicalClusterNbElts(cluster, wss);

    // Create a variable stream for the cluster
    ZL_Output* const zStream = ZL_Encoder_createStringStream(
            eictx, outcomeInfo.idx, nbElts, nbBytes);
    if (zStream == nullptr)
        throw std::bad_alloc();
    uint8_t* contentPtr = (uint8_t*)ZL_Output_ptr(zStream);
    [[maybe_unused]] const uint8_t* const contentEnd = contentPtr + nbBytes;
    uint32_t* fieldSizesPtr = ZL_Output_stringLens(zStream);
    [[maybe_unused]] const uint32_t* const fieldSizesEnd =
            fieldSizesPtr + nbElts;

    // Concatenate data into the variable stream
    for (LogicalId id : cluster.idList) {
        const WriteStream& contentChunk      = wss.getStream(id);
        const WriteStream& stringLengthChunk = wss.getStringLengthStream(id);
        const uint32_t chunkSizeElts =
                stringLengthChunk.nbytes() / sizeof(uint32_t);
        sanityCheckStringLengthsDebugOnly(
                stringLengthChunk, contentChunk.nbytes(), chunkSizeElts);

        if (contentChunk.nbytes() > 0) {
            assert(contentPtr + contentChunk.nbytes() <= contentEnd);
            folly::MutableByteRange chunkRange{ contentPtr,
                                                contentChunk.nbytes() };
            contentChunk.copyTo(chunkRange);
            contentPtr += contentChunk.nbytes();
        }
        if (stringLengthChunk.nbytes() > 0) {
            assert(fieldSizesPtr + chunkSizeElts <= fieldSizesEnd);
            folly::MutableByteRange chunkRange{
                (uint8_t*)fieldSizesPtr, chunkSizeElts * sizeof(uint32_t)
            };
            stringLengthChunk.copyTo(chunkRange);
            fieldSizesPtr += chunkSizeElts;
        }

        clusterLengths.writeValue<uint32_t>(chunkSizeElts);
    }
    if (ZL_isError(ZL_Output_commit(zStream, nbElts))) {
        throw std::runtime_error{ "Failed to commit Zstrong stream" };
    }

    // Attach successor metadata
    if (ZL_isError(ZL_Output_setIntMetadata(
                zStream, kDirectedSelectorMetadataID, cluster.successor))) {
        throw std::runtime_error{ "Failed to set directed selector metadata" };
    }

    return zStream;
}

template <typename Parser>
ZL_Report configurableEncode(ZL_Encoder* eictx, const ZL_Input* in)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(eictx);
    unsigned int const formatVersion =
            ZL_Encoder_getCParam(eictx, ZL_CParam_formatVersion);
    ZL_ERR_IF_LT(
            formatVersion, kMinFormatVersionEncode, formatVersion_unsupported);

    try {
        ZL_ASSERT(ZL_Input_type(in) == ZL_Type_serial);
        const folly::ByteRange srcRange = { (const uint8_t*)ZL_Input_ptr(in),
                                            ZL_Input_numElts(in) };

        // Read config
        ZL_CopyParam gp = ZL_Encoder_getLocalCopyParam(eictx, 0);
        ZL_ERR_IF_EQ(gp.paramId, ZL_LP_INVALID_PARAMID, corruption);
        std::string_view const encoderConfigStr(
                (const char*)gp.paramPtr, gp.paramSize);
        EncoderConfig config = EncoderConfig(encoderConfigStr);

        // Fail compression if config uses unsupported features
        ZL_ERR_IF_LT(
                formatVersion,
                config.getMinFormatVersion(),
                formatVersion_unsupported);

        // Temporary requirement: ensure contiguous LogicalIds
        // (starting from 0)
        for (const auto& streamId : config.getLogicalIds()) {
            static_assert(std::is_unsigned_v<std::underlying_type_t<
                                  std::decay_t<decltype(streamId)>>>);
            ZL_ERR_IF_GE(
                    static_cast<size_t>(streamId),
                    config.getLogicalIds().size(),
                    temporaryLibraryLimitation);
        }

        // Encode the input stream!
        ReadStream srcStream{ srcRange };
        WriteStreamSet dstStreamSet(config, formatVersion);
        Parser parser{ config, srcStream, dstStreamSet, formatVersion };
        try {
            parser.parse();
        } catch (const std::exception& ex) {
            ZL_ERR(GENERIC,
                   "Thrift kernel failed inside core parser: %s",
                   ex.what());
        }
        debug("Encoder side:");
        debug(srcStream.repr());
        debug(dstStreamSet.repr());

        // Serialize config for decoder.
        {
            std::string const decoderConfigStr =
                    DecoderConfig(
                            config,
                            srcRange.size(),
                            config.getShouldParseTulipV2())
                            .serialize();
            assert(dstStreamSet.getStream(SingletonId::kConfig).nbytes() == 0);
            dstStreamSet.getStream(SingletonId::kConfig).setWidth(1);
            dstStreamSet.getStream(SingletonId::kConfig)
                    .writeBytes(folly::ByteRange{ decoderConfigStr });
        }

        // Finalize all WriteStreams in dstStreamSet.
        // Note: writes to dstStreamSet past this point are not allowed!
        dstStreamSet.closeAllStreams();
        auto typeSuccessorMap = config.getTypeSuccessorMap();
        // Create and commit singleton streams
        for (size_t i = 0; i < (size_t)SingletonId::kNumSingletonIds; i++) {
            const WriteStream& ws = dstStreamSet.getStream(SingletonId(i));
            ZL_Output* zs =
                    copyFixedWidthWriteStreamToEICtx(eictx, ws, SingletonId(i));
            auto typeInfo = getTypeInfo(ws.type(), kMinFormatVersionStringVSF);
            int successor = typeSuccessorMap[typeInfo.ztype];
            if (ZL_isError(ZL_Output_setIntMetadata(
                        zs, kDirectedSelectorMetadataID, successor))) {
                throw std::runtime_error{
                    "Failed to set directed selector metadata"
                };
            }
        }

        // Create and commit unclustered logical streams
        for (LogicalId id : config.getUnclusteredStreams()) {
            const WriteStream& ws = dstStreamSet.getStream(id);
            ZL_Output* zs;

            if (ws.type() == TType::T_STRING
                && formatVersion >= kMinFormatVersionStringVSF) {
                const auto& lengths = dstStreamSet.getStringLengthStream(id);
                zs = copyStringWriteStreamToEICtx(eictx, ws, lengths);
            } else {
                zs = copyFixedWidthWriteStreamToEICtx(eictx, ws, id);
            }

            auto successor = config.getSuccessorForLogicalStream(id);
            if (successor.has_value()) {
                if (ZL_isError(ZL_Output_setIntMetadata(
                            zs,
                            kDirectedSelectorMetadataID,
                            successor.value()))) {
                    throw std::runtime_error{
                        "Failed to set directed selector metadata"
                    };
                }
            }
        }

        // Create and commit clustered streams
        WriteStream clusterLengths(TType::T_U32);
        clusterLengths.setWidth(sizeof(uint32_t));
        for (const LogicalCluster& cluster : config.clusters()) {
            assert(formatVersion >= kMinFormatVersionEncodeClusters);
            if (getClusterType(cluster, dstStreamSet) == TType::T_STRING
                && formatVersion >= kMinFormatVersionStringVSF) {
                copyStringLogicalClusterToEICtx(
                        eictx, cluster, dstStreamSet, clusterLengths);
            } else {
                copyFixedWidthLogicalClusterToEICtx(
                        eictx,
                        cluster,
                        dstStreamSet,
                        clusterLengths,
                        formatVersion);
            }
        }
        clusterLengths.close();

        // Create and commit ClusterLengths stream
        //
        // NOTE: ClusterLengths *must* be the final variable stream, as
        // that's where the decoder looks for it.
        if (config.clusters().size() > 0) {
            assert(formatVersion >= kMinFormatVersionEncodeClusters);
            assert(clusterLengths.nbytes() % clusterLengths.width() == 0);
            assert(clusterLengths.width() == sizeof(uint32_t));
            size_t const nbElts = clusterLengths.nbytes() / sizeof(uint32_t);
            const OutcomeInfo outcomeInfo =
                    getOutcomeInfo(VariableOutcome::kClusterSegmentLengths);
            ZL_Output* const zStream = createZstrongStream(
                    eictx, outcomeInfo.idx, nbElts, clusterLengths.width());
            copyFixedWidthWriteStreamToZstrongStream(
                    zStream,
                    clusterLengths,
                    ZL_validResult(ZL_Output_contentCapacity(zStream)));
            if (ZL_isError(ZL_Output_commit(zStream, nbElts))) {
                throw std::runtime_error{ "Failed to commit Zstrong stream" };
            }
        }

        return ZL_returnSuccess();
    } catch (const std::exception& ex) {
        ZL_ERR(GENERIC,
               "Thrift kernel failed outside of core parsing: %s",
               ex.what());
    }
}

template <typename DParser>
ZL_Report configurableDecode(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    const unsigned int formatVersion = DI_getFrameFormatVersion(dictx);
    ZL_ERR_IF_LT(
            formatVersion, kMinFormatVersionDecode, formatVersion_unsupported);

    assert(nbCompulsorySrcs == (size_t)SingletonId::kNumSingletonIds);
    try {
        // Deserialize config
        const ZL_Input* configZStream = // terrelln-codemod-skip
                compulsorySrcs[size_t(SingletonId::kConfig)];
        assert(ZL_Input_type(configZStream) == ZL_Type_serial);
        folly::ByteRange const configRange{
            (const uint8_t*)ZL_Input_ptr(configZStream),
            ZL_Input_contentSize(configZStream)
        };
        DecoderConfig const config(configRange);

        // Set up input and output streams
        ReadStreamSet srcStreams{ config,           compulsorySrcs,
                                  nbCompulsorySrcs, variableSrcs,
                                  nbVariableSrcs,   formatVersion };
        ZSDecodeWriteStream dstStream{ dictx, config.getOriginalSize() };

        // Decode the input streams!
        DParser parser{
            config, srcStreams, dstStream.writeStream(), formatVersion
        };
        try {
            parser.unparse();
        } catch (const std::exception& ex) {
            ZL_ERR(GENERIC,
                   "Thrift kernel failed inside core parser: %s",
                   ex.what());
        }
        debug("Decoder side:");
        debug(srcStreams.repr());
        debug(dstStream.writeStream().repr());

        ZL_ERR_IF_ERR(dstStream.commit());

        return ZL_returnValue(1);
    } catch (const std::exception& ex) {
        ZL_ERR(GENERIC,
               "Thrift kernel failed outside of core parsing: %s",
               ex.what());
    }
}

ZL_Report configurableEncodeCompact(
        ZL_Encoder* eictx,
        const ZL_Input* in) noexcept
{
    return configurableEncode<CompactParser>(eictx, in);
}

ZL_Report configurableEncodeBinary(
        ZL_Encoder* eictx,
        const ZL_Input* in) noexcept
{
    return configurableEncode<BinaryParser>(eictx, in);
}

ZL_Report configurableDecodeCompact(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs) noexcept
{
    return configurableDecode<DCompactParser>(
            dictx,
            compulsorySrcs,
            nbCompulsorySrcs,
            variableSrcs,
            nbVariableSrcs);
}

ZL_Report configurableDecodeBinary(
        ZL_Decoder* dictx,
        const ZL_Input* compulsorySrcs[],
        size_t nbCompulsorySrcs,
        const ZL_Input* variableSrcs[],
        size_t nbVariableSrcs) noexcept
{
    return configurableDecode<DBinaryParser>(
            dictx,
            compulsorySrcs,
            nbCompulsorySrcs,
            variableSrcs,
            nbVariableSrcs);
}

std::array<
        ZL_Type,
        size_t(SingletonId::
                       kNumSingletonIds)> constexpr kSingletonOutcomeTypes =
        [] {
            auto a = decltype(kSingletonOutcomeTypes){};
            for (size_t i = 0; i < a.size(); i++) {
                a[i] = getOutcomeInfo(SingletonId(i)).type;
            }
            return a;
        }();

std::array<
        ZL_Type,
        size_t(VariableOutcome::
                       kNumVariableOutcomes)> constexpr kVariableOutcomeTypes =
        [] {
            auto a = decltype(kVariableOutcomeTypes){};
            for (size_t i = 0; i < a.size(); i++) {
                a[i] = getOutcomeInfo(VariableOutcome(i)).type;
            }
            return a;
        }();

ZL_VOGraphDesc const thriftCompactConfigurableGd = {
    .CTid           = kThriftCompactConfigurable,
    .inStreamType   = ZL_Type_serial,
    .singletonTypes = kSingletonOutcomeTypes.data(),
    .nbSingletons   = kSingletonOutcomeTypes.size(),
    .voTypes        = kVariableOutcomeTypes.data(),
    .nbVOs          = kVariableOutcomeTypes.size(),
};

ZL_VOGraphDesc const thriftBinaryConfigurableGd = {
    .CTid           = kThriftBinaryConfigurable,
    .inStreamType   = ZL_Type_serial,
    .singletonTypes = kSingletonOutcomeTypes.data(),
    .nbSingletons   = kSingletonOutcomeTypes.size(),
    .voTypes        = kVariableOutcomeTypes.data(),
    .nbVOs          = kVariableOutcomeTypes.size(),
};

} // namespace

ZL_VOEncoderDesc const thriftCompactConfigurableSplitter = {
    .gd          = thriftCompactConfigurableGd,
    .transform_f = configurableEncodeCompact,
    .name        = "Thrift Compact Encode",
};

ZL_VODecoderDesc const thriftCompactConfigurableUnSplitter = {
    .gd          = thriftCompactConfigurableGd,
    .transform_f = configurableDecodeCompact,
    .name        = "Thrift Compact Decode",
};

ZL_VOEncoderDesc const thriftBinaryConfigurableSplitter = {
    .gd          = thriftBinaryConfigurableGd,
    .transform_f = configurableEncodeBinary,
    .name        = "Thrift Binary Encode",
};

ZL_VODecoderDesc const thriftBinaryConfigurableUnSplitter = {
    .gd          = thriftBinaryConfigurableGd,
    .transform_f = configurableDecodeBinary,
    .name        = "Thrift Binary Decode",
};

ZL_Report registerCustomTransforms(ZL_DCtx* dctx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dctx);
    ZL_ERR_IF_ERR(ZL_DCtx_registerVODecoder(
            dctx, &thriftCompactConfigurableUnSplitter));
    ZL_ERR_IF_ERR(ZL_DCtx_registerVODecoder(
            dctx, &thriftBinaryConfigurableUnSplitter));
    return ZL_returnSuccess();
}

ZL_NodeID registerCompactTransform(ZL_Compressor* cgraph, ZL_IDType id)
{
    auto desc    = thriftCompactConfigurableSplitter;
    desc.gd.CTid = id;
    return ZL_Compressor_registerVOEncoder(cgraph, &desc);
}

ZL_Report registerCompactTransform(ZL_DCtx* dctx, ZL_IDType id)
{
    auto desc    = thriftCompactConfigurableUnSplitter;
    desc.gd.CTid = id;
    return ZL_DCtx_registerVODecoder(dctx, &desc);
}

ZL_NodeID registerBinaryTransform(ZL_Compressor* cgraph, ZL_IDType id)
{
    auto desc    = thriftBinaryConfigurableSplitter;
    desc.gd.CTid = id;
    return ZL_Compressor_registerVOEncoder(cgraph, &desc);
}

ZL_Report registerBinaryTransform(ZL_DCtx* dctx, ZL_IDType id)
{
    auto desc    = thriftBinaryConfigurableUnSplitter;
    desc.gd.CTid = id;
    return ZL_DCtx_registerVODecoder(dctx, &desc);
}

ZL_NodeID cloneThriftNodeWithLocalParams(
        ZL_Compressor* cgraph,
        ZL_NodeID nodeId,
        std::string_view serializedConfig)
{
    const ZL_CopyParam gp            = { .paramId   = 0,
                                         .paramPtr  = serializedConfig.data(),
                                         .paramSize = serializedConfig.size() };
    const ZL_LocalParams localParams = { .copyParams = { .copyParams   = &gp,
                                                         .nbCopyParams = 1 } };
    return ZL_Compressor_cloneNode(cgraph, nodeId, &localParams);
}

} // namespace zstrong::thrift
