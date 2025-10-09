// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/tulip_v2/encode_tulip_v2.h"

#include <exception>
#include <span>
#include <vector>

#include <folly/ScopeGuard.h>
#include <folly/io/Cursor.h>
#include <folly/io/IOBuf.h>
#include <thrift/lib/cpp2/protocol/CompactProtocol.h>

#include "custom_transforms/thrift/kernels/encode_thrift_binding.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_selector.h"

namespace zstrong::tulip_v2 {
namespace {
using apache::thrift::CompactProtocolReader;
using apache::thrift::TType;

size_t constexpr kMaxDepth = 32;

class TulipV2Parser {
   public:
    explicit TulipV2Parser(folly::IOBuf const& buf)
    {
        reader_.setInput(&buf);
    }

    std::vector<std::pair<Tag, size_t>> parse() &&
    {
        while (readTulipV2Header()) {
            readStruct();
        }
        commit(Tag::EverythingElse, reader_.getCursorPosition());
        return std::move(committedSegments_);
    }

   private:
    bool readTulipV2Header()
    {
        // No more input
        if (reader_.getCursor().isAtEnd()) {
            return false;
        }

        int8_t byte0;
        reader_.readByte(byte0);
        // Allow newline separators
        if (byte0 == int8_t('\n') || byte0 == 0) {
            // Trailing newline or 0 is okay
            if (reader_.getCursor().isAtEnd()) {
                return false;
            }
            reader_.readByte(byte0);
        }
        int8_t byte1;
        reader_.readByte(byte1);
        if (byte0 != int8_t(0x80) || byte1 != int8_t(0x00)) {
            throw std::runtime_error(fmt::format(
                    "Bad TulipV2 Header {} {}: {}",
                    byte0,
                    byte1,
                    reader_.getCursorPosition()));
        }
        return true;
    }

    void readStruct()
    {
        reader_.readStructBegin(ignore_);
        TType fieldType;
        int16_t fieldId;

        reader_.readFieldBegin(ignore_, fieldType, fieldId);
        while (fieldType != TType::T_STOP) {
            readValue(fieldType);
            reader_.readFieldEnd();
            reader_.readFieldBegin(ignore_, fieldType, fieldId);
        }

        reader_.readStructEnd();
    }

    void readValue(TType type)
    {
        if (depth_ > kMaxDepth) {
            throw std::runtime_error("Max depth exceeded!");
        }
        ++depth_;
        SCOPE_EXIT
        {
            --depth_;
        };
        switch (type) {
            default:
                throw std::runtime_error("Invalid type!");
            case TType::T_BOOL: {
                bool value;
                reader_.readBool(value);
                break;
            }
            case TType::T_BYTE: {
                int8_t value;
                reader_.readByte(value);
                break;
            }
            case TType::T_I16: {
                int16_t value;
                reader_.readI16(value);
                break;
            }
            case TType::T_I32: {
                int32_t value;
                reader_.readI32(value);
                break;
            }
            case TType::T_I64: {
                int64_t value;
                reader_.readI64(value);
                break;
            }
            case TType::T_DOUBLE: {
                double value;
                reader_.readDouble(value);
                break;
            }
            case TType::T_STRING:
                reader_.readString(ignore_);
                break;
            case TType::T_LIST:
                readList();
                break;
            case TType::T_SET:
                readSet();
                break;
            case TType::T_MAP:
                readMap();
                break;
            case TType::T_STRUCT:
                readStruct();
                break;
            case TType::T_FLOAT: {
                float value;
                reader_.readFloat(value);
                break;
            }
        }
    }

    void readList()
    {
        TType elementType;
        uint32_t size;
        reader_.readListBegin(elementType, size);
        readCollection(elementType, size);
        reader_.readListEnd();
    }

    void readSet()
    {
        TType elementType;
        uint32_t size;
        reader_.readSetBegin(elementType, size);
        readCollection(elementType, size);
        reader_.readSetEnd();
    }

    void readCollection(TType elementType, uint32_t size)
    {
        for (uint32_t i = 0; i < size; ++i) {
            readValue(elementType);
        }
    }

    void readMap()
    {
        size_t const mapBeginPos = reader_.getCursorPosition();

        TType keyType, valueType;
        uint32_t size;
        reader_.readMapBegin(keyType, valueType, size);
        auto const mapTag = peekMapTag(keyType, valueType, size);
        for (uint32_t i = 0; i < size; ++i) {
            readValue(keyType);
            readValue(valueType);
        }
        reader_.readMapEnd();

        if (mapTag != Tag::EverythingElse && !posIsCommitted(mapBeginPos)) {
            size_t const mapEndPos = reader_.getCursorPosition();
            commit(Tag::EverythingElse, mapBeginPos);
            commit(mapTag, mapEndPos);
        }
    }

    Tag peekMapTag(TType keyType, TType valueType, uint32_t size) const
    {
        // TODO: We need to detect our special map types and separate them.
        // We can peek here to determine the type
        if (keyType != TType::T_I32) {
            return Tag::EverythingElse;
        }
        if (valueType == TType::T_FLOAT) {
            return Tag::FloatFeatures;
        }
        if (size == 0) {
            return Tag::EverythingElse;
        }
        if (!(valueType == TType::T_LIST || valueType == TType::T_MAP)) {
            return Tag::EverythingElse;
        }

        // Read the map until we have enough information to determine the type.
        CompactProtocolReader peeker;
        peeker.setInput(reader_.getCursor());
        for (size_t i = 0; i < size; ++i) {
            int32_t key;
            peeker.readI32(key);
            if (valueType == TType::T_MAP) {
                TType innerKeyType, innerValueType;
                uint32_t innerSize;
                peeker.readMapBegin(innerKeyType, innerValueType, innerSize);
                if (innerSize != 0) {
                    if (innerKeyType == TType::T_I64
                        && innerValueType == TType::T_FLOAT) {
                        return Tag::IdScoreListFeatures;
                    } else {
                        return Tag::EverythingElse;
                    }
                }
                peeker.readMapEnd();
            } else {
                assert(valueType == TType::T_LIST);
                TType innerType;
                uint32_t innerSize;
                peeker.readListBegin(innerType, innerSize);
                if (innerType == TType::T_I64) {
                    return Tag::IdListFeatures;
                }
                if (innerType == TType::T_FLOAT) {
                    return Tag::FloatListFeatures;
                }
                if (innerType != TType::T_LIST) {
                    return Tag::EverythingElse;
                }
                if (innerSize != 0) {
                    TType innerInnerType;
                    uint32_t innerInnerSize;
                    peeker.readListBegin(innerInnerType, innerInnerSize);
                    if (innerInnerType == TType::T_I64) {
                        return Tag::IdListListFeatures;
                    } else {
                        return Tag::EverythingElse;
                    }
                }
                peeker.readListEnd();
            }
        }
        // We could get here if:
        //   1. map<map> and all inner maps are empty
        //   2. map<list> and all inner lists are empty
        // Just treat it as everything else, it is either small or highly
        // compressible.
        return Tag::EverythingElse;
    }

    bool posIsCommitted(size_t pos) const
    {
        return pos < committedPosition_;
    }

    void commit(Tag tag, size_t pos)
    {
        assert(!posIsCommitted(pos));
        // Don't commit empty segments
        if (pos == committedPosition_) {
            return;
        }

        size_t const size = pos - committedPosition_;

        if (!committedSegments_.empty()
            && committedSegments_.back().first == tag) {
            committedSegments_.back().second += size;
        } else {
            committedSegments_.emplace_back(tag, size);
        }

        committedPosition_ = pos;
    }

    CompactProtocolReader reader_;
    std::vector<std::pair<Tag, size_t>> committedSegments_;
    size_t committedPosition_ = 0;
    size_t depth_             = 0;
    std::string ignore_;
};

ZL_DispatchInstructions parseTulipV2(
        ZL_DispatchState* state,
        ZL_Input const* in)
{
    try {
        assert(ZL_Input_type(in) == ZL_Type_serial);

        auto buf = folly::IOBuf::wrapBufferAsValue(
                ZL_Input_ptr(in), ZL_Input_numElts(in));
        auto parse = TulipV2Parser(buf).parse();

        auto segmentSizes = (size_t*)ZL_DispatchState_malloc(
                state, parse.size() * sizeof(size_t));
        auto tags = (unsigned*)ZL_DispatchState_malloc(
                state, parse.size() * sizeof(uint32_t));

        if (segmentSizes == nullptr || tags == nullptr) {
            return ZL_DispatchState_returnError(state, "bad alloc");
        }

        for (size_t i = 0; i < parse.size(); ++i) {
            tags[i]         = static_cast<unsigned>(parse[i].first);
            segmentSizes[i] = parse[i].second;
        }

        return { segmentSizes, tags, parse.size(), unsigned(Tag::NumTags) };
    } catch (std::exception const& e) {
        return ZL_DispatchState_returnError(state, e.what());
    }
}

ZL_GraphID selectTulipV2(
        ZL_Selector const*,
        ZL_Input const* in,
        ZL_GraphID const* successors,
        size_t nbSuccessors) noexcept
{
    if (ZL_Input_numElts(in) == 0)
        return ZL_GRAPH_STORE;
    auto const metadata = ZL_Input_getIntMetadata(in, ZL_DISPATCH_CHANNEL_ID);
    assert(metadata.isPresent);
    assert(metadata.mValue >= 0);
    assert(metadata.mValue < nbSuccessors);
    (void)nbSuccessors;
    return successors[metadata.mValue];
}

ZL_GraphID declareSelector(
        ZL_Compressor* cgraph,
        ZL_SelectorFn selectorFn,
        ZL_Type inType,
        std::span<ZL_GraphID const> successors)
{
    ZL_SelectorDesc desc = {
        .selector_f     = selectorFn,
        .inStreamType   = inType,
        .customGraphs   = successors.data(),
        .nbCustomGraphs = successors.size(),
        .localParams    = {},
    };

    return ZL_Compressor_registerSelectorGraph(cgraph, &desc);
}

ZL_GraphID declareGraph(
        ZL_Compressor* cgraph,
        ZL_NodeID node,
        std::initializer_list<ZL_GraphID> successors)
{
    return ZL_Compressor_registerStaticGraph_fromNode(
            cgraph, node, successors.begin(), successors.size());
}

ZL_GraphID floatGraph(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerFieldLZGraph(cgraph);
}

ZL_GraphID idGraph(ZL_Compressor* cgraph, bool sorted)
{
    // TODO: Do something smarter if sorted?
    (void)sorted;
    auto graph = ZL_Compressor_registerFieldLZGraph(cgraph);
    return graph;
}

ZL_GraphID lengthsGraph(ZL_Compressor* cgraph)
{
    auto graph = declareGraph(
            cgraph, ZL_NODE_QUANTIZE_LENGTHS, { ZL_GRAPH_FSE, ZL_GRAPH_STORE });
    return graph;
}

ZL_GraphID defaultFloatFeatures(
        ZL_Compressor* cgraph,
        unsigned customTransformId)
{
    auto const node = ZL_ThriftKernel_registerCTransformMapI32Float(
            cgraph, customTransformId);
    return declareGraph(
            cgraph,
            node,
            { ZL_GRAPH_ZSTD, featureIDsGraph(cgraph), floatGraph(cgraph) });
}

ZL_GraphID defaultIdListFeatures(
        ZL_Compressor* cgraph,
        unsigned customTransformId)
{
    auto const node = ZL_ThriftKernel_registerCTransformMapI32ArrayI64(
            cgraph, customTransformId);
    return declareGraph(
            cgraph,
            node,
            { ZL_GRAPH_ZSTD,
              featureIDsGraph(cgraph),
              lengthsGraph(cgraph),
              idGraph(cgraph, false) });
}

ZL_GraphID defaultIdListListFeatures(
        ZL_Compressor* cgraph,
        unsigned customTransformId)
{
    auto const node = ZL_ThriftKernel_registerCTransformMapI32ArrayArrayI64(
            cgraph, customTransformId);
    return declareGraph(
            cgraph,
            node,
            { ZL_GRAPH_ZSTD,
              featureIDsGraph(cgraph),
              lengthsGraph(cgraph),
              lengthsGraph(cgraph),
              idGraph(cgraph, false) });
}

ZL_GraphID defaultFloatListFeatures(
        ZL_Compressor* cgraph,
        unsigned customTransformId)
{
    auto const node = ZL_ThriftKernel_registerCTransformMapI32ArrayFloat(
            cgraph, customTransformId);
    return declareGraph(
            cgraph,
            node,
            { ZL_GRAPH_ZSTD,
              featureIDsGraph(cgraph),
              lengthsGraph(cgraph),
              floatGraph(cgraph) });
}

ZL_GraphID defaultIdScoreListFeatures(
        ZL_Compressor* cgraph,
        unsigned customTransformId)
{
    auto const node = ZL_ThriftKernel_registerCTransformMapI32MapI64Float(
            cgraph, customTransformId);
    return declareGraph(
            cgraph,
            node,
            { ZL_GRAPH_ZSTD,
              featureIDsGraph(cgraph),
              lengthsGraph(cgraph),
              idGraph(cgraph, true),
              floatGraph(cgraph) });
}

ZL_GraphID defaultEverythingElse(ZL_Compressor*, unsigned customTransformId)
{
    assert(customTransformId == unsigned(-1));
    (void)customTransformId;
    return ZL_GRAPH_ZSTD;
}

template <typename Fn>
ZL_GraphID graphOr(
        ZL_Compressor* cgraph,
        unsigned customTransformId,
        std::optional<ZL_GraphID> graph,
        Fn&& fn)
{
    if (graph.has_value()) {
        return graph.value();
    } else {
        return fn(cgraph, customTransformId);
    }
}

ZL_GraphID declareTulipV2Graph(
        ZL_Compressor* cgraph,
        ZL_NodeID dispatch,
        ZL_GraphID selector)
{
    return declareGraph(
            cgraph,
            dispatch,
            { ZL_GRAPH_BITPACK_INT, ZL_GRAPH_BITPACK_INT, selector });
}
} // namespace

std::vector<std::pair<Tag, size_t>> parseTulipV2(std::string_view input)
{
    auto buf = folly::IOBuf::wrapBufferAsValue(folly::StringPiece(input));
    return TulipV2Parser(buf).parse();
}

ZL_GraphID featureIDsGraph(ZL_Compressor* cgraph)
{
    auto fieldLz     = ZL_Compressor_registerFieldLZGraph(cgraph);
    auto bitsetGraph = fieldLz;
    auto mergedGraph = declareGraph(
            cgraph, ZL_NODE_QUANTIZE_LENGTHS, { ZL_GRAPH_FSE, ZL_GRAPH_STORE });
    mergedGraph = declareGraph(cgraph, ZL_NODE_DELTA_INT, { mergedGraph });

    auto backupGraph = fieldLz;
    backupGraph      = declareGraph(cgraph, ZL_NODE_DELTA_INT, { fieldLz });
    backupGraph =
            declareGraph(cgraph, ZL_NODE_TOKENIZE, { fieldLz, backupGraph });

    return ZL_Compressor_registerMergeSortedGraph(
            cgraph, bitsetGraph, mergedGraph, backupGraph);
}

ZL_NodeID createTulipV2Node(ZL_Compressor* cgraph)
{
    return ZL_Compressor_registerDispatchNode(cgraph, parseTulipV2, nullptr);
}

ZL_GraphID createTulipV2Graph(
        ZL_Compressor* cgraph,
        TulipV2Successors const& successors,
        unsigned idRangeBegin,
        unsigned idRangeEnd)
{
    auto dispatch = createTulipV2Node(cgraph);
    auto selector = createTulipV2SuccessorSelector(
            cgraph, successors, idRangeBegin, idRangeEnd);
    return declareTulipV2Graph(cgraph, dispatch, selector);
}

ZL_GraphID createTulipV2SuccessorSelector(
        ZL_Compressor* cgraph,
        TulipV2Successors const& successors,
        unsigned idRangeBegin,
        unsigned idRangeEnd)
{
    if (idRangeEnd - idRangeBegin < kNumCustomTransforms) {
        throw std::runtime_error("Not enough IDs");
    }
    std::array<ZL_GraphID, size_t(Tag::NumTags)> succ;
    succ[size_t(Tag::FloatFeatures)] =
            graphOr(cgraph,
                    idRangeBegin + unsigned(Tag::FloatFeatures),
                    successors.floatFeatures,
                    defaultFloatFeatures);
    succ[size_t(Tag::IdListFeatures)] =
            graphOr(cgraph,
                    idRangeBegin + unsigned(Tag::IdListFeatures),
                    successors.idListFeatures,
                    defaultIdListFeatures);
    succ[size_t(Tag::IdListListFeatures)] =
            graphOr(cgraph,
                    idRangeBegin + unsigned(Tag::IdListListFeatures),
                    successors.idListListFeatures,
                    defaultIdListListFeatures);
    succ[size_t(Tag::FloatListFeatures)] =
            graphOr(cgraph,
                    idRangeBegin + unsigned(Tag::FloatListFeatures),
                    successors.floatListFeatures,
                    defaultFloatListFeatures);
    succ[size_t(Tag::IdScoreListFeatures)] =
            graphOr(cgraph,
                    idRangeBegin + unsigned(Tag::IdScoreListFeatures),
                    successors.idScoreListFeatures,
                    defaultIdScoreListFeatures);
    succ[size_t(Tag::EverythingElse)] =
            graphOr(cgraph,
                    unsigned(-1),
                    successors.everythingElse,
                    defaultEverythingElse);

    return declareSelector(cgraph, selectTulipV2, ZL_Type_serial, succ);
}

} // namespace zstrong::tulip_v2
