// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/zstrong_cpp.h"

#include <chrono>
#include <cstdio>
#include <functional>
#include <numeric>
#include <ratio>
#include <utility>

#include "custom_transforms/json_extract/decode_json_extract.h"
#include "custom_transforms/json_extract/encode_json_extract.h"
#include "custom_transforms/parse/decode_parse.h"
#include "custom_transforms/parse/encode_parse.h"
#include "custom_transforms/thrift/directed_selector.h"
#include "custom_transforms/thrift/thrift_parsers.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/shared/mem.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_reflection.h"

namespace zstrong {
namespace {
constexpr ZL_IDType kJsonExtractTransformID  = 310;
constexpr ZL_IDType kParseInt64TransformID   = 311;
constexpr ZL_IDType kParseFloat64TransformID = 312;

template <typename T>
const T* getSelf(ZL_Encoder const* eictx)
{
    const auto self = (const T*)ZL_Encoder_getOpaquePtr(eictx);
    if (self == nullptr) {
        throw std::runtime_error("Self pointer is null");
    }
    return self;
}

template <typename T>
T const* getSelf(ZL_Selector const* selCtx)
{
    const auto self = (const T*)ZL_Selector_getOpaquePtr(selCtx);
    if (self == nullptr) {
        throw std::runtime_error("Self pointer is null");
    }
    return self;
}

template <typename T>
T const* getSelf(ZL_Decoder const* dictx)
{
    const auto self = (const T*)ZL_Decoder_getOpaquePtr(dictx);
    if (self == nullptr) {
        throw std::runtime_error("Self pointer is null");
    }
    return self;
}

ZL_OpaquePtr makeOpaqueRef(const void* self)
{
    return { const_cast<void*>(self), NULL, NULL };
}

template <typename T>
ZL_OpaquePtr makeOpaqueOwn(std::shared_ptr<const T> self)
{
    return { const_cast<void*>((const void*)self.get()),
             new std::shared_ptr<const T>(self),
             [](void* opaque, void* ptr) noexcept {
                 auto shared = (std::shared_ptr<const T>*)opaque;
                 assert(shared != nullptr);
                 assert(shared->get() == ptr);
                 delete shared;
             } };
}

class StandardTransform : public ParameterizedTransform {
   public:
    StandardTransform(
            ZL_NodeID node,
            std::string description,
            std::vector<std::string> successorNames = {},
            std::vector<ParamInfo> intParams        = {},
            std::vector<ParamInfo> genericParams    = {})
            : node_(node),
              description_(std::move(description)),
              successorNames_(std::move(successorNames)),
              intParams_(std::move(intParams)),
              genericParams_(std::move(genericParams))
    {
    }

    ZL_GraphID registerTransform(
            ZL_Compressor& cgraph,
            std::span<ZL_GraphID const> successors,
            ZL_LocalParams const& params) const override
    {
        ZL_NodeID node = node_;
        if (params.genericParams.nbCopyParams > 0
            || params.intParams.nbIntParams > 0) {
            // Only override if parameters were set, otherwise we may lose
            // parameters that were already set on the standard node.
            // TODO: Standard nodes probably shouldn't "internally" use
            // parameters like convert_serial_to_token2 does.
            const ZL_ParameterizedNodeDesc desc = {
                .node        = node_,
                .localParams = &params,
            };
            node = ZL_Compressor_registerParameterizedNode(&cgraph, &desc);
        }
        return ZL_Compressor_registerStaticGraph_fromNode(
                &cgraph, node, successors.data(), successors.size());
    }

    void registerTransform(ZL_DCtx&) const override {}

    size_t nbInputs() const override
    {
        // NOTE: We currently need a cgraph to get access to metadata.
        CGraph cgraph;
        return ZL_Compressor_Node_getNumInputs(cgraph.get(), node_);
    }

    size_t nbSuccessors() const override
    {
        // NOTE: We currently need a cgraph to get access to metadata.
        CGraph cgraph;
        return ZL_Compressor_Node_getNumOutcomes(cgraph.get(), node_);
    }

    ZL_Type inputType(size_t idx) const override
    {
        // NOTE: We currently need a cgraph to get access to metadata.
        CGraph cgraph;
        return ZL_Compressor_Node_getInputType(cgraph.get(), node_, idx);
    }

    ZL_Type outputType(size_t idx) const override
    {
        // NOTE: We currently need a cgraph to get access to metadata.
        CGraph cgraph;
        return ZL_Compressor_Node_getOutputType(cgraph.get(), node_, (int)idx);
    }

    std::string description() const override
    {
        return description_;
    }

    std::string successorName(size_t idx) const override
    {
        if (successorNames_.empty()) {
            if (nbSuccessors() == 1) {
                return "successor";
            } else {
                return "successor" + std::to_string(idx);
            }
        }
        return successorNames_[idx];
    }

    std::vector<ParamInfo> const& intParams() const override
    {
        return intParams_;
    }

    std::vector<ParamInfo> const& genericParams() const override
    {
        return genericParams_;
    }

   private:
    ZL_NodeID node_;
    std::string description_;
    std::vector<std::string> successorNames_;
    std::vector<ParamInfo> intParams_;
    std::vector<ParamInfo> genericParams_;
};

class StandardFnTransform : public ParameterizedTransform {
   public:
    StandardFnTransform(
            std::function<ZL_GraphID(
                    ZL_Compressor&,
                    std::span<ZL_GraphID const>,
                    ZL_LocalParams const&)> transformFn,
            std::vector<ZL_Type> inputTypes,
            std::vector<ZL_Type> outputTypes,
            std::string description,
            std::vector<std::string> successorNames = {},
            std::vector<ParamInfo> intParams        = {},
            std::vector<ParamInfo> genericParams    = {})
            : transformFn_(std::move(transformFn)),
              inputTypes_(std::move(inputTypes)),
              outputTypes_(std::move(outputTypes)),
              description_(std::move(description)),
              successorNames_(std::move(successorNames)),
              intParams_(std::move(intParams)),
              genericParams_(std::move(genericParams))
    {
    }

    ZL_GraphID registerTransform(
            ZL_Compressor& cgraph,
            std::span<ZL_GraphID const> successors,
            ZL_LocalParams const& params) const override
    {
        return transformFn_(cgraph, successors, params);
    }

    void registerTransform(ZL_DCtx&) const override {}

    size_t nbInputs() const override
    {
        return inputTypes_.size();
    }

    size_t nbSuccessors() const override
    {
        return outputTypes_.size();
    }

    ZL_Type inputType(size_t idx) const override
    {
        return inputTypes_.at(idx);
    }

    ZL_Type outputType(size_t idx) const override
    {
        return outputTypes_.at(idx);
    }

    std::string description() const override
    {
        return description_;
    }

    std::string successorName(size_t idx) const override
    {
        if (successorNames_.empty()) {
            if (nbSuccessors() == 1) {
                return "successor";
            } else {
                return "successor" + std::to_string(idx);
            }
        }
        return successorNames_[idx];
    }

    std::vector<ParamInfo> const& intParams() const override
    {
        return intParams_;
    }

    std::vector<ParamInfo> const& genericParams() const override
    {
        return genericParams_;
    }

   private:
    std::function<ZL_GraphID(
            ZL_Compressor&,
            std::span<ZL_GraphID const>,
            ZL_LocalParams const&)>
            transformFn_;
    std::vector<ZL_Type> inputTypes_;
    std::vector<ZL_Type> outputTypes_;
    std::string description_;
    std::vector<std::string> successorNames_;
    std::vector<ParamInfo> intParams_;
    std::vector<ParamInfo> genericParams_;
};

class ThriftTransform : public StandardFnTransform {
    static std::vector<ZL_Type> thriftOutTypes(bool compact)
    {
        std::vector<ZL_Type> outTypes;
        auto const& desc = compact ? thrift::thriftCompactConfigurableSplitter
                                   : thrift::thriftBinaryConfigurableSplitter;
        outTypes.insert(
                outTypes.end(),
                desc.gd.singletonTypes,
                desc.gd.singletonTypes + desc.gd.nbSingletons);
        outTypes.insert(
                outTypes.end(),
                desc.gd.voTypes,
                desc.gd.voTypes + desc.gd.nbVOs);
        return outTypes;
    }

   public:
    explicit ThriftTransform(bool compact)
            : StandardFnTransform(
                      [compact](auto& cgraph, auto successors, auto params) {
                          ZL_NodeID node;
                          if (compact) {
                              node = registerCompactTransform(
                                      &cgraph,
                                      thrift::kThriftCompactConfigurable);
                          } else {
                              node = registerBinaryTransform(
                                      &cgraph,
                                      thrift::kThriftBinaryConfigurable);
                          }
                          const ZL_ParameterizedNodeDesc desc = {
                              .node        = node,
                              .localParams = &params,
                          };
                          node = ZL_Compressor_registerParameterizedNode(
                                  &cgraph, &desc);
                          return ZL_Compressor_registerStaticGraph_fromNode(
                                  &cgraph,
                                  node,
                                  successors.data(),
                                  successors.size());
                      },
                      { ZL_Type_serial },
                      thriftOutTypes(compact),
                      "Thrift parser",
                      {},
                      {},
                      { ParamInfo{ 0, "config", "Thrift parser config" } }),
              compact_(compact)
    {
    }

    size_t nbVariableSuccessors() const override
    {
        auto const& desc = compact_ ? thrift::thriftCompactConfigurableSplitter
                                    : thrift::thriftBinaryConfigurableSplitter;
        return desc.gd.nbVOs;
    }

   private:
    bool compact_;
};

class StandardGraph : public Graph {
   public:
    StandardGraph(ZL_GraphID graph, std::string description)
            : graph_(graph), description_(std::move(description))
    {
    }

    ZL_GraphID registerGraph(ZL_Compressor&) const override
    {
        return graph_;
    }

    void registerGraph(ZL_DCtx&) const override {}

    ZL_Type inputType() const override
    {
        // NOTE: We currently need a cgraph to get access to metadata.
        CGraph cgraph;
        return ZL_Compressor_Graph_getInput0Mask(cgraph.get(), graph_);
    }

    std::string description() const override
    {
        return description_;
    }

   private:
    ZL_GraphID graph_;
    std::string description_;
};

class StandardFnGraph : public Graph {
   public:
    StandardFnGraph(
            std::function<ZL_GraphID(ZL_Compressor*)> graphFn,
            std::string description)
            : graphFn_(std::move(graphFn)), description_(std::move(description))
    {
    }

    ZL_GraphID registerGraph(ZL_Compressor& cgraph) const override
    {
        return graphFn_(&cgraph);
    }

    void registerGraph(ZL_DCtx&) const override {}

    ZL_Type inputType() const override
    {
        // NOTE: We currently need a cgraph to get access to metadata.
        CGraph cgraph;
        auto const graph = registerGraph(*cgraph);
        return ZL_Compressor_Graph_getInput0Mask(cgraph.get(), graph);
    }

    std::string description() const override
    {
        return description_;
    }

   private:
    std::function<ZL_GraphID(ZL_Compressor*)> graphFn_;
    std::string description_;
};

class BruteForceSelector : public CustomSelector {
   public:
    explicit BruteForceSelector() = default;

    ZL_Type inputType() const override
    {
        return ZL_Type_any;
    }

    std::string description() const override
    {
        return "Selects the best option among all the successors and store by brute force";
    }

    ZL_GraphID select(
            ZL_Selector const* selCtx,
            ZL_Input const* input,
            std::span<ZL_GraphID const> successors) const override
    {
        size_t bestSize = ZL_Input_numElts(input) * ZL_Input_eltWidth(input);
        ZL_GraphID best = ZL_GRAPH_STORE;
        for (auto const successor : successors) {
            auto const result = ZL_Selector_tryGraph(selCtx, input, successor);
            if (ZL_isError(result.finalCompressedSize))
                continue;
            size_t const size = ZL_validResult(result.finalCompressedSize);
            if (size < bestSize) {
                bestSize = size;
                best     = successor;
            }
        }
        return best;
    }
};

class DirectedSelector : public Selector {
   public:
    DirectedSelector() = default;

    ZL_GraphID registerSelector(
            ZL_Compressor& cgraph,
            std::span<ZL_GraphID const> successors,
            ZL_LocalParams const& localParams) const override
    {
        auto desc = buildDirectedSelectorDesc(
                ZL_Type_any, successors.data(), successors.size());
        desc.localParams = localParams;
        return ZL_Compressor_registerSelectorGraph(&cgraph, &desc);
    }

    ZL_Type inputType() const override
    {
        return ZL_Type_any;
    }

    virtual std::string description() const override
    {
        return "Dispatches to the output stream directed by the input stream's "
               "int metadata with key 0. NOTE: The input MUST have integer "
               "metadata for key 0, and its value must be at least zero and "
               "less than the number of successors.";
    }

    ~DirectedSelector() override = default;
};

static std::mutex extractFileMutex;

class ExtractSelector : public CustomSelector {
   public:
    explicit ExtractSelector() = default;

    std::optional<size_t> expectedNbSuccessors() const override
    {
        return 1;
    }

    ZL_Type inputType() const override
    {
        return ZL_Type_any;
    }

    std::string description() const override
    {
        return "Extracts the input stream to a file and forwards it to the successor. "
               "NOTE: The string_param with key 1 must be the path to extract to. "
               "Streams will be appended to this file with the format "
               "<1-byte type><8-byte LE nbElts><8-byte LE eltWidth><data>.";
    }

    ZL_GraphID select(
            ZL_Selector const* selCtx,
            ZL_Input const* input,
            std::span<ZL_GraphID const> successors) const override
    {
        auto const param = ZL_Selector_getLocalCopyParam(selCtx, 1);
        if (param.paramId == ZL_LP_INVALID_PARAMID) {
            throw std::runtime_error("Output path parameter not set");
        }
        uint64_t const nbElts     = ZL_Input_numElts(input);
        uint64_t const eltWidth   = ZL_Input_eltWidth(input);
        uint64_t const inputBytes = nbElts * eltWidth;
        std::string path{ (char const*)param.paramPtr, param.paramSize };

        std::string data;
        data.resize(17);
        data[0] = (char)ZL_Input_type(input);
        ZL_writeLE64(data.data() + 1, nbElts);
        ZL_writeLE64(data.data() + 9, eltWidth);
        data.append((char const*)ZL_Input_ptr(input), inputBytes);

        {
            // TODO: This serializes all the file writes. If this becomes a
            // bottleneck we can try finer grained locking, or splitting
            // into multiple files.
            std::lock_guard<std::mutex> g(extractFileMutex);
            FILE* f = std::fopen(path.c_str(), "ab");
            if (f == nullptr) {
                throw std::runtime_error("Failed to write file: " + path);
            }
            size_t const w = std::fwrite(data.data(), 1, data.size(), f);
            std::fclose(f);
            if (w != data.size()) {
                throw std::runtime_error("Failed to write file: " + path);
            }
        }
        return successors[0];
    }
};

} // namespace

ParameterizedTransformMap const& getStandardTransforms()
{
    auto const createStandardTransforms = [] {
        ParameterizedTransformMap transforms;
        transforms.emplace(
                "delta_int",
                std::make_unique<StandardTransform>(
                        ZL_NODE_DELTA_INT,
                        "This transform stores the first value 'raw', and then "
                        "each value is a delta from the previous value."));
        transforms.emplace(
                "transpose_split",
                std::make_unique<StandardTransform>(
                        ZL_NODE_TRANSPOSE_SPLIT,
                        "Convert a stream of N fields of size S into S serial "
                        "streams of size N."));
        transforms.emplace(
                "zigzag",
                std::make_unique<StandardTransform>(
                        ZL_NODE_ZIGZAG,
                        "This transform converts a distribution of signed "
                        "values centered around 0 into a series of purely "
                        "positive numbers."));
        transforms.emplace(
                "float32_deconstruct",
                std::make_unique<StandardTransform>(
                        ZL_NODE_FLOAT32_DECONSTRUCT,
                        "Takes a series of float32 and separates them into a "
                        "fixed-size stream 0 containing the sign & fraction "
                        "bits, and a fixed-size stream 1 containing the "
                        "exponent bits.",
                        std::vector<std::string>{ "sign_frac", "exponent" }));
        transforms.emplace(
                "bfloat16_deconstruct",
                std::make_unique<StandardTransform>(
                        ZL_NODE_BFLOAT16_DECONSTRUCT,
                        "Takes a series of bfloat16 and separates them into a "
                        "fixed-size stream 0 containing the sign & fraction "
                        "bits, and a fixed-size stream 1 containing the "
                        "exponent bits.",
                        std::vector<std::string>{ "sign_frac", "exponent" }));
        transforms.emplace(
                "float16_deconstruct",
                std::make_unique<StandardTransform>(
                        ZL_NODE_FLOAT16_DECONSTRUCT,
                        "Takes a series of float16 and separates them into a "
                        "fixed-size stream 0 containing the sign & fraction "
                        "bits, and a fixed-size stream 1 containing the "
                        "exponent bits.",
                        std::vector<std::string>{ "sign_frac", "exponent" }));
        transforms.emplace(
                "field_lz_with_custom_graphs",
                std::make_unique<StandardTransform>(
                        ZL_NODE_FIELD_LZ,
                        "Compresses a fixed-size stream using LZ compression "
                        "that matches entire fields. Stream 0 contains the "
                        "literals. "
                        "Stream 1 contains the tokens (10-bit values). "
                        "Stream 2 contains the offsets (non-zero u32). "
                        "Stream 3 contains the extra literal lengths (u32). "
                        "Stream 4 contains the extra match lengths (u32).",
                        std::vector<std::string>{ "literals",
                                                  "tokens",
                                                  "offsets",
                                                  "extra_literal_lengths",
                                                  "extra_match_lengths" }));
        transforms.emplace(
                "convert_serial_to_token",
                std::make_unique<StandardTransform>(
                        ZL_NODE_CONVERT_SERIAL_TO_TOKENX,
                        "Converts a serial stream to a token stream. "
                        "NOTE: Requires that the int_param with key `1` be the "
                        "token size.",
                        std::vector<std::string>{ "successor" },
                        std::vector<ParamInfo>{
                                { 1,
                                  "elt_width",
                                  "The size of each token in bytes" } }));
        transforms.emplace(
                "convert_serial_to_token2",
                std::make_unique<StandardTransform>(
                        ZL_NODE_CONVERT_SERIAL_TO_TOKEN2,
                        "Converts a serial stream to a token stream of size 2."));
        transforms.emplace(
                "convert_serial_to_token4",
                std::make_unique<StandardTransform>(
                        ZL_NODE_CONVERT_SERIAL_TO_TOKEN4,
                        "Converts a serial stream to a token stream of size 4."));
        transforms.emplace(
                "convert_serial_to_token8",
                std::make_unique<StandardTransform>(
                        ZL_NODE_CONVERT_SERIAL_TO_TOKEN8,
                        "Converts a serial stream to a token stream of size 8."));
        transforms.emplace(
                "convert_token_to_serial",
                std::make_unique<StandardTransform>(
                        ZL_NODE_CONVERT_TOKEN_TO_SERIAL,
                        "Converts a token stream to a serial stream. "
                        "NOTE: This is can be inferred implicitly."));
        transforms.emplace(
                "convert_num_to_token",
                std::make_unique<StandardTransform>(
                        ZL_NODE_CONVERT_NUM_TO_TOKEN,
                        "Converts a numeric stream to a token stream. "
                        "NOTE: This is can be inferred implicitly."));
        transforms.emplace(
                "interpret_token_as_le",
                std::make_unique<StandardTransform>(
                        ZL_NODE_INTERPRET_TOKEN_AS_LE,
                        "Interprets a token stream as LE integers."));
        transforms.emplace(
                "interpret_as_le8",
                std::make_unique<StandardTransform>(
                        ZL_NODE_INTERPRET_AS_LE8,
                        "Interprets a numeric stream as 8-bit LE integers."));
        transforms.emplace(
                "interpret_as_le16",
                std::make_unique<StandardTransform>(
                        ZL_NODE_INTERPRET_AS_LE16,
                        "Interprets a numeric stream as 16-bit LE integers."));
        transforms.emplace(
                "interpret_as_le32",
                std::make_unique<StandardTransform>(
                        ZL_NODE_INTERPRET_AS_LE32,
                        "Interprets a numeric stream as 32-bit LE integers."));
        transforms.emplace(
                "interpret_as_le64",
                std::make_unique<StandardTransform>(
                        ZL_NODE_INTERPRET_AS_LE64,
                        "Interprets a numeric stream as 64-bit LE integers."));
        transforms.emplace(
                "bitunpack",
                std::make_unique<StandardTransform>(
                        ZS2_NODE_BITUNPACK,
                        "Converts a serial stream of packed integers into a "
                        "numeric stream. The number of bytes must be exact, "
                        "but any leftover bits can be any value. "
                        "NOTE: Requires the int_param with key `1` be the "
                        "bit-width of the values.",
                        std::vector<std::string>{ "successor" },
                        std::vector<ParamInfo>{
                                { 1,
                                  "bit_width",
                                  "The width of each packed int in bits" } }));
        transforms.emplace(
                "range_pack",
                std::make_unique<StandardTransform>(
                        ZL_NODE_RANGE_PACK,
                        "Subtracts the minimum (unsigned) element from all "
                        "other elements in the stream and outputs the minimal "
                        "size numeric stream that can contain the 0-based "
                        "range of values."));
        transforms.emplace(
                "quantize_offsets",
                std::make_unique<StandardTransform>(
                        ZL_NODE_QUANTIZE_OFFSETS,
                        "Quantizes u32 values into a bucket and extra bits. "
                        "Uses a power of two scheme to determine the buckets. "
                        "WARNING: 0 is not allowed",
                        std::vector<std::string>{ "buckets", "bits" }));
        transforms.emplace(
                "quantize_lengths",
                std::make_unique<StandardTransform>(
                        ZL_NODE_QUANTIZE_LENGTHS,
                        "Quantizes u32 values into a bucket and extra bits. Gives "
                        "small values singleton buckets then falls back to a power "
                        "of two scheme.",
                        std::vector<std::string>{ "buckets", "bits" }));
        transforms.emplace(
                "separate_vsf_components",
                std::make_unique<StandardTransform>(
                        ZL_NODE_SEPARATE_STRING_COMPONENTS,
                        "Separates variable size fields into two streams: "
                        "The content (serialized), and the sizes (u32).",
                        std::vector<std::string>{ "content", "field_sizes" }));
        transforms.emplace(
                "prefix",
                std::make_unique<StandardTransform>(
                        ZL_NODE_PREFIX,
                        "Removes shared prefix from successive variable size fields",
                        std::vector<std::string>{ "suffixes",
                                                  "prefix_sizes" }));
        transforms.emplace(
                "concat_serial",
                std::make_unique<StandardTransform>(
                        ZL_NODE_CONCAT_SERIAL,
                        "Concatenates N serial streams into one. Returns 2 streams, "
                        "one containing the lengths of each input stream, and the other "
                        "the result of the concatenation",
                        std::vector<std::string>{ "lengths", "concatenated" }));
        transforms.emplace(
                "tokenize",
                std::make_unique<StandardFnTransform>(
                        [](auto& cgraph, auto successors, auto const& params) {
                            if (params.intParams.nbIntParams != 2) {
                                throw std::runtime_error(
                                        "Invalid # of int params");
                            }
                            auto const streamIdx =
                                    params.intParams.intParams[0].paramId == 0
                                    ? 0
                                    : 1;
                            const auto streamType =
                                    (ZL_Type)params.intParams
                                            .intParams[streamIdx]
                                            .paramValue;
                            auto const sort =
                                    params.intParams.intParams[!streamIdx]
                                            .paramValue;
                            return ZL_Compressor_registerTokenizeGraph(
                                    &cgraph,
                                    streamType,
                                    sort,
                                    successors[0],
                                    successors[1]);
                        },
                        std::vector<ZL_Type>{
                                ZL_Type(ZL_Type_struct | ZL_Type_string
                                        | ZL_Type_numeric) },
                        std::vector<ZL_Type>{ ZL_Type(ZL_Type_struct
                                                      | ZL_Type_string
                                                      | ZL_Type_numeric),
                                              ZL_Type_numeric },
                        "Tokenizes the input into an alphabet stream and an indicies stream. "
                        "The alphabet is either sorted in ascending order, or in occurrence order.",
                        std::vector<std::string>{ "alphabet", "indices" },
                        std::vector<ParamInfo>{
                                { 0, "stream_type", "The input stream type" },
                                { 1,
                                  "sort",
                                  "Should we sort in ascending order?" } }));
        auto graphFn = [](ZL_Compressor& cgraph,
                          std::span<ZL_GraphID const> successors,
                          ZL_LocalParams const&) {
            return ZL_Compressor_registerFieldLZGraph_withLiteralsGraph(
                    &cgraph, successors[0]);
        };
        transforms.emplace(
                "field_lz_with_literals_graph",
                std::make_unique<StandardFnTransform>(
                        std::move(graphFn),
                        std::vector<ZL_Type>{ ZL_Type_struct },
                        std::vector<ZL_Type>{ ZL_Type_struct },
                        "Compresses a fixed-size stream using LZ compression "
                        "that matches entire fields, with a custom literals "
                        "graph.",
                        std::vector<std::string>{ "literals" }));
        // TODO(terrelln): These don't belong here, but lets get this up and
        // working first...
        transforms.emplace(
                "thrift_compact", std::make_unique<ThriftTransform>(true));
        transforms.emplace(
                "thrift_binary", std::make_unique<ThriftTransform>(false));
        transforms.emplace(
                "json_extract",
                std::make_unique<StandardFnTransform>(
                        [](ZL_Compressor& cgraph,
                           std::span<ZL_GraphID const> successors,
                           ZL_LocalParams const&) {
                            auto node = ZS2_Compressor_registerJsonExtract(
                                    &cgraph, kJsonExtractTransformID);
                            return ZL_Compressor_registerStaticGraph_fromNode(
                                    &cgraph,
                                    node,
                                    successors.data(),
                                    successors.size());
                        },
                        std::vector<ZL_Type>{ ZL_Type_serial },
                        std::vector<ZL_Type>{ ZL_Type_serial,
                                              ZL_Type_string,
                                              ZL_Type_string,
                                              ZL_Type_string },
                        "Json Extract",
                        std::vector<std::string>{
                                "json", "ints", "floats", "strs" }));
        transforms.emplace(
                "parse_int64",
                std::make_unique<StandardFnTransform>(
                        [](ZL_Compressor& cgraph,
                           std::span<ZL_GraphID const> successors,
                           ZL_LocalParams const&) {
                            auto node = ZS2_Compressor_registerParseInt64(
                                    &cgraph, kParseInt64TransformID);
                            return ZL_Compressor_registerStaticGraph_fromNode(
                                    &cgraph,
                                    node,
                                    successors.data(),
                                    successors.size());
                        },
                        std::vector<ZL_Type>{ ZL_Type_string },
                        std::vector<ZL_Type>{ ZL_Type_numeric,
                                              ZL_Type_numeric,
                                              ZL_Type_string },
                        "Parse ints",
                        std::vector<std::string>{
                                "int64s", "exception indices", "exceptions" }));
        transforms.emplace(
                "parse_float64",
                std::make_unique<StandardFnTransform>(
                        [](ZL_Compressor& cgraph,
                           std::span<ZL_GraphID const> successors,
                           ZL_LocalParams const&) {
                            auto node = ZS2_Compressor_registerParseFloat64(
                                    &cgraph, kParseFloat64TransformID);
                            return ZL_Compressor_registerStaticGraph_fromNode(
                                    &cgraph,
                                    node,
                                    successors.data(),
                                    successors.size());
                        },
                        std::vector<ZL_Type>{ ZL_Type_string },
                        std::vector<ZL_Type>{ ZL_Type_numeric,
                                              ZL_Type_numeric,
                                              ZL_Type_string },
                        "Parse floats",
                        std::vector<std::string>{ "float64s",
                                                  "exception indices",
                                                  "exceptions" }));
        return transforms;
    };
    static auto* standardTransforms = new ParameterizedTransformMap(
            std::invoke(createStandardTransforms));
    return *standardTransforms;
}

GraphMap const& getStandardGraphs()
{
    static auto* standardGraphs = new GraphMap(std::invoke([] {
        GraphMap graphs;
        graphs.emplace(
                "store",
                std::make_unique<StandardGraph>(
                        ZL_GRAPH_STORE, "Stores the input stream as-is."));
        graphs.emplace(
                "constant",
                std::make_unique<StandardGraph>(
                        ZL_GRAPH_CONSTANT, "Constant encoding."));
        graphs.emplace(
                "fse",
                std::make_unique<StandardGraph>(
                        ZL_GRAPH_FSE, "FSE entropy compression."));
        graphs.emplace(
                "huffman",
                std::make_unique<StandardGraph>(
                        ZL_GRAPH_HUFFMAN,
                        "Huffman entropy compression of serial data."));
        graphs.emplace(
                "huffman_fixed",
                std::make_unique<StandardGraph>(
                        ZL_GRAPH_HUFFMAN,
                        "Huffman entropy compression of fixed-size data of width 1 or 2."));
        graphs.emplace(
                "zstd",
                std::make_unique<StandardGraph>(
                        ZL_GRAPH_ZSTD, "zstd compression."));
        graphs.emplace(
                "bitpack",
                std::make_unique<StandardGraph>(
                        ZL_GRAPH_BITPACK, "Bitpack integer or serial data."));
        graphs.emplace(
                "flatpack",
                std::make_unique<StandardGraph>(
                        ZL_GRAPH_FLATPACK,
                        "Fast tokenize + bitpack of serial data"));
        graphs.emplace(
                "generic_lz",
                std::make_unique<StandardGraph>(
                        ZL_GRAPH_GENERIC_LZ_BACKEND,
                        "A generic LZ compression backend"));
        graphs.emplace(
                "generic_compress",
                std::make_unique<StandardGraph>(
                        ZL_GRAPH_COMPRESS_GENERIC,
                        "A generic compression backend"));
        graphs.emplace(
                "entropy",
                std::make_unique<StandardGraph>(
                        ZL_GRAPH_ENTROPY,
                        "A generic entropy compression backend"));

        graphs.emplace(
                "field_lz",
                std::make_unique<StandardFnGraph>(
                        ZL_Compressor_registerFieldLZGraph,
                        "LZ compressor that specializes in compressing fixed-size "
                        "fields."));

        return graphs;
    }));
    return *standardGraphs;
}

SelectorMap const& getStandardSelectors()
{
    static auto* standardSelectors = new SelectorMap(std::invoke([] {
        SelectorMap selectors;
        selectors.emplace(
                "brute_force", std::make_unique<BruteForceSelector>());
        selectors.emplace("extract", std::make_unique<ExtractSelector>());
        selectors.emplace("directed", std::make_unique<DirectedSelector>());
        return selectors;
    }));
    return *standardSelectors;
}

MIGraphDesc CustomTransform::graphDesc() const
{
    size_t const nbInStreams = nbInputs();
    std::vector<ZL_Type> inStreamTypes;
    inStreamTypes.reserve(nbInStreams);
    for (size_t i = 0; i < nbInStreams; ++i) {
        inStreamTypes.push_back(inputType(i));
    }
    size_t const nbOutStreams = nbSuccessors();
    std::vector<ZL_Type> outStreamTypes;
    outStreamTypes.reserve(nbOutStreams);
    for (size_t i = 0; i < nbOutStreams; ++i) {
        outStreamTypes.push_back(outputType(i));
    }
    ZL_MIGraphDesc desc = {
        .CTid     = transformID_,
        .nbInputs = nbInputs(),
        .nbSOs    = nbFixedSuccessors(),
        .nbVOs    = nbVariableSuccessors(),
    };
    return MIGraphDesc(
            desc, std::move(inStreamTypes), std::move(outStreamTypes));
}

ZL_GraphID CustomTransform::registerTransform(
        ZL_Compressor& cgraph,
        std::span<ZL_GraphID const> successors,
        ZL_LocalParams const& params) const
{
    auto const gd       = graphDesc();
    auto const encodeFn = [](ZL_Encoder* eictx,
                             ZL_Input const* inputs[],
                             size_t nbInputs) noexcept {
        auto self = getSelf<CustomTransform>(eictx);
        return self->encode(eictx, inputs, nbInputs);
    };
    ZL_MIEncoderDesc desc = {
        .gd          = *gd,
        .transform_f = encodeFn,
        .localParams = params,
        .opaque      = makeOpaqueRef(this),
    };
    auto const node = ZL_Compressor_registerMIEncoder(&cgraph, &desc);
    return ZL_Compressor_registerStaticGraph_fromNode(
            &cgraph, node, successors.data(), successors.size());
}

void CustomTransform::registerTransform(ZL_DCtx& dctx) const
{
    auto const gd       = graphDesc();
    auto const decodeFn = [](ZL_Decoder* dictx,
                             ZL_Input const* fixedInputs[],
                             size_t const nbFixed,
                             ZL_Input const* voInputs[],
                             size_t nbVO) noexcept {
        auto self = getSelf<CustomTransform>(dictx);
        return self->decode(dictx, fixedInputs, nbFixed, voInputs, nbVO);
    };
    ZL_MIDecoderDesc desc = {
        .gd          = *gd,
        .transform_f = decodeFn,
        .opaque      = { (void*)this, nullptr, nullptr },
    };
    unwrap(ZL_DCtx_registerMIDecoder(&dctx, &desc),
           "Failed to register custom decoder");
}

ZL_Report CustomTransform::encode(ZL_Encoder* eictx, ZL_Input const* input)
{
    return encode(eictx, &input, 1);
}

ZL_Report CustomTransform::decode(ZL_Decoder* dictx, ZL_Input const*[]) const
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ERR(logicError);
}

ZL_Report CustomTransform::decode(
        ZL_Decoder* dictx,
        ZL_Input const* fixedInputs[],
        size_t nbFixed,
        ZL_Input const*[],
        size_t nbVO) const
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    assert(nbFixed == nbFixedSuccessors());
    (void)nbFixed;
    if (nbVariableSuccessors() == 0) {
        ZL_ERR_IF_NE(nbVO, 0, node_invalid_input);
        return this->decode(dictx, fixedInputs);
    }
    ZL_ERR(logicError);
}

ZL_GraphID CustomSelector::registerSelector(
        ZL_Compressor& cgraph,
        std::span<ZL_GraphID const> successors,
        const ZL_LocalParams& params) const
{
    auto const selectFn = [](ZL_Selector const* selCtx,
                             ZL_Input const* input,
                             ZL_GraphID const* customSelectors,
                             size_t nbCustomSuccessors) noexcept {
        auto self = getSelf<CustomSelector const>(selCtx);
        return self->select(
                selCtx,
                input,
                std::span{ customSelectors, nbCustomSuccessors });
    };

    ZL_SelectorDesc desc = {
        .selector_f     = selectFn,
        .inStreamType   = inputType(),
        .customGraphs   = successors.data(),
        .nbCustomGraphs = successors.size(),
        .localParams    = params,
        .opaque         = makeOpaqueRef(this),
    };

    return ZL_Compressor_registerSelectorGraph(&cgraph, &desc);
}

ZL_GraphID registerOwnedSelector(
        ZL_Compressor& cgraph,
        std::shared_ptr<CustomSelector const> selector,
        std::span<ZL_GraphID const> successors,
        const ZL_LocalParams& params,
        std::string_view name)
{
    auto const selectFn = [](ZL_Selector const* selCtx,
                             ZL_Input const* input,
                             ZL_GraphID const* customSelectors,
                             size_t nbCustomSuccessors) noexcept {
        auto self = getSelf<CustomSelector const>(selCtx);
        return self->select(
                selCtx,
                input,
                std::span{ customSelectors, nbCustomSuccessors });
    };

    ZL_SelectorDesc desc = {
        .selector_f     = selectFn,
        .inStreamType   = selector->inputType(),
        .customGraphs   = successors.data(),
        .nbCustomGraphs = successors.size(),
        .localParams    = params,
        .name           = name.size() ? name.begin() : nullptr,
        .opaque         = makeOpaqueOwn(std::move(selector)),
    };

    return ZL_Compressor_registerSelectorGraph(&cgraph, &desc);
}

std::string CGraph::compress(
        std::string_view data,
        std::optional<std::unordered_map<ZL_CParam, int>> const& globalParams)
{
    std::vector<std::string_view> dataVec{ data };
    return compressMulti(dataVec, globalParams);
}

std::string CGraph::compressMulti(
        std::vector<std::string_view> const& data,
        std::optional<std::unordered_map<ZL_CParam, int>> const& globalParams)
{
    CCtx cctx;
    return zstrong::compressMulti(cctx, data, *this, globalParams);
}

std::string compress(
        std::string_view data,
        Graph const& graph,
        std::optional<std::unordered_map<ZL_CParam, int>> const& globalParams)
{
    std::vector<std::string_view> dataVec{ data };
    return compressMulti(dataVec, graph, globalParams);
}

std::string compressMulti(
        const std::vector<std::string_view>& data,
        Graph const& graph,
        std::optional<std::unordered_map<ZL_CParam, int>> const& globalParams)
{
    CGraph cgraph;
    auto const graphID = graph.registerGraph(*cgraph);
    cgraph.unwrap(ZL_Compressor_selectStartingGraphID(cgraph.get(), graphID));
    return cgraph.compressMulti(data, globalParams);
}

std::string compress(
        std::string_view data,
        ZL_GraphID const graphID,
        std::optional<std::unordered_map<ZL_CParam, int>> const& globalParams)
{
    CGraph cgraph;
    cgraph.unwrap(ZL_Compressor_selectStartingGraphID(cgraph.get(), graphID));
    return cgraph.compress(data, globalParams);
}

std::string compress(
        CCtx& cctx,
        std::string_view data,
        CGraph const& cgraph,
        std::optional<std::unordered_map<ZL_CParam, int>> const& globalParams)
{
    return compressMulti(cctx, { data }, cgraph, globalParams);
}

std::string compressMulti(
        CCtx& cctx,
        std::vector<std::string_view> data,
        CGraph const& cgraph,
        std::optional<std::unordered_map<ZL_CParam, int>> const& globalParams)
{
    std::string compressed;
    compressMulti(cctx, &compressed, std::move(data), cgraph, globalParams);
    return compressed;
}

void compress(
        CCtx& cctx,
        std::string* out,
        std::string_view data,
        CGraph const& graph,
        std::optional<std::unordered_map<ZL_CParam, int>> const& globalParams)
{
    compressMulti(cctx, out, { data }, graph, globalParams);
}

void compressMulti(
        CCtx& cctx,
        std::string* out,
        std::vector<std::string_view> data,
        CGraph const& cgraph,
        std::optional<std::unordered_map<ZL_CParam, int>> const& globalParams)
{
    // TODO (terrelln): Give a way to set the format version.
    // For now this is just experimental code so set it to max.
    cctx.setParameter(openzl::CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    // Disable automatic store for small Streams
    cctx.setParameter(openzl::CParam::MinStreamSize, -1);
    if (globalParams.has_value()) {
        for (auto const [param, value] : globalParams.value()) {
            cctx.setParameter(openzl::CParam(param), value);
        }
    }
    cctx.refCompressor(cgraph);

    size_t totalInputSz = std::accumulate(
            data.begin(),
            data.end(),
            0ul,
            [](const size_t a, const std::string_view b) {
                return a + b.size();
            });
    if (auto const bound = ZL_compressBound(totalInputSz);
        out->size() < bound) {
        out->resize(bound);
    }
    std::vector<TypedRef> inputs;
    for (const auto& input : data) {
        inputs.push_back(TypedRef::refSerial(input));
    }

    out->resize(cctx.compress(*out, inputs));
}

std::string decompress(std::string_view data)
{
    auto ret = decompressMulti(data);
    if (ret.size() != 1) {
        throw std::runtime_error(
                "Decompression failed. Expected 1 output but got "
                + std::to_string(ret.size()));
    }
    return ret[0];
}

std::string decompress(std::string_view data, Graph const& graph)
{
    auto ret = decompressMulti(data, graph);
    if (ret.size() != 1) {
        throw std::runtime_error(
                "Decompression failed. Expected 1 output but got "
                + std::to_string(ret.size()));
    }
    return ret[0];
}

std::string
decompress(DCtx& dctx, std::string_view data, std::optional<size_t> maxDstSize)
{
    auto ret = decompressMulti(dctx, data, maxDstSize);
    if (ret.size() != 1) {
        throw std::runtime_error(
                "Decompression failed. Expected 1 output but got "
                + std::to_string(ret.size()));
    }
    return ret[0];
}

std::vector<std::string> decompressMulti(std::string_view compressed)
{
    DCtx dctx;
    return decompressMulti(dctx, compressed);
}

std::vector<std::string> decompressMulti(
        std::string_view compressed,
        Graph const& graph)
{
    DCtx dctx;
    // TODO(terrelln): Get rid of this hack by properly registering thrift as a
    // custom transform. We don't currently support custom C++ transforms.
    dctx.unwrap(
            thrift::registerCompactTransform(
                    dctx.get(), thrift::kThriftCompactConfigurable));
    dctx.unwrap(
            thrift::registerBinaryTransform(
                    dctx.get(), thrift::kThriftBinaryConfigurable));
    dctx.unwrap(
            ZS2_DCtx_registerJsonExtract(dctx.get(), kJsonExtractTransformID));
    dctx.unwrap(
            ZS2_DCtx_registerParseInt64(dctx.get(), kParseInt64TransformID));
    dctx.unwrap(ZS2_DCtx_registerParseFloat64(
            dctx.get(), kParseFloat64TransformID));
    graph.registerGraph(*dctx);
    return decompressMulti(dctx, compressed, std::nullopt);
}

std::vector<std::string> decompressMulti(
        DCtx& dctx,
        std::string_view compressed,
        std::optional<size_t> maxDstSize)
{
    openzl::FrameInfo info(compressed);
    const auto numOutputs = info.numOutputs();

    std::vector<std::string> decompressed;
    std::vector<openzl::Output> outputs;
    decompressed.reserve(numOutputs);
    for (size_t i = 0; i < numOutputs; ++i) {
        const auto contentSize = info.outputContentSize(i);
        if (maxDstSize.has_value() && contentSize > *maxDstSize) {
            throw std::runtime_error("Output size too large");
        }
        decompressed.emplace_back(contentSize, '\0');
        outputs.emplace_back(openzl::Output::wrapSerial(decompressed.back()));
    }
    dctx.decompress(outputs, compressed);
    for (size_t i = 0; i < numOutputs; ++i) {
        assert(outputs[i].contentSize() == decompressed[i].size());
    }
    return decompressed;
}

size_t getHeaderSize(std::string_view data)
{
    return unwrap(ZL_getHeaderSize(data.data(), data.size()));
}

namespace {
std::vector<double> measureDecompressionSpeedsInner(
        std::vector<std::string_view> const& compressed,
        Graph const* graph)
{
    size_t const kNumIterations = 10;
    DCtx dctx;
    if (graph != nullptr) {
        graph->registerGraph(*dctx);
    }
    std::vector<double> result(compressed.size());
    for (size_t c = 0; c < compressed.size(); c++) {
        std::string decompressed;
        size_t const decompressedSize = unwrap(ZL_getDecompressedSize(
                compressed[c].data(), compressed[c].size()));
        decompressed.resize(decompressedSize);

        auto const timerStart = std::chrono::steady_clock::now();
        size_t size           = 0;
        for (size_t i = 0; i < kNumIterations; ++i) {
            size = dctx.unwrap(ZL_DCtx_decompress(
                    dctx.get(),
                    decompressed.data(),
                    decompressed.size(),
                    compressed[c].data(),
                    compressed[c].size()));
        }
        std::chrono::duration<double, std::milli> const timeElapsedMS =
                (std::chrono::steady_clock::now() - timerStart);
        double const timeElapsedS    = timeElapsedMS.count() / 1000.;
        double const iterationSizeMB = (double)size / (double)(1024. * 1024.);
        double const totalSizeMB     = iterationSizeMB * (double)kNumIterations;
        result[c]                    = totalSizeMB / timeElapsedS;
    }
    return result;
}
} // namespace

std::vector<double> measureDecompressionSpeeds(
        const std::vector<std::string_view>& compressed)
{
    return measureDecompressionSpeedsInner(std::move(compressed), nullptr);
}

std::vector<double> measureDecompressionSpeeds(
        const std::vector<std::string_view>& compressed,
        Graph const& graph)
{
    return measureDecompressionSpeedsInner(std::move(compressed), &graph);
}

double measureDecompressionSpeed(std::string_view compressed)
{
    return measureDecompressionSpeedsInner({ compressed }, nullptr)[0];
}

double measureDecompressionSpeed(
        std::string_view compressed,
        Graph const& graph)
{
    return measureDecompressionSpeedsInner({ compressed }, &graph)[0];
}

} // namespace zstrong
