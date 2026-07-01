// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/dev/contrib/gpu/testkit/multichunk_frame.h"

#include <cstdint>
#include <vector>

#include "openzl/zl_common_types.h" // ZL_TernaryParam_disable
#include "openzl/zl_errors.h"
#include "openzl/zl_localParams.h" // ZL_IntParam, ZL_LocalParams
#include "openzl/zl_segmenter.h"   // ZL_Compressor_registerSegmenter, ...
#include "openzl/zl_selector.h"    // ZL_LP_INVALID_PARAMID
#include "tests/utils.h"           // ZL_COMPRESSBOUND_UNGUARDED

namespace openzl::gpu::testkit {

namespace {

// MinStreamSize follows the convention that 0 means "default"; only a NEGATIVE
// threshold fully disables the per-stream "automatic store" feature. Mirrors
// frame_factory.cpp.
constexpr int kDisableMinStreamSize = -1;

// Local-int-param ID plane for handing the per-chunk plan to the segmenter
// callback. ID kNumChunksParamId holds the chunk count; ID
// kChunkEltsParamIdBase + i holds chunk i's element count. These IDs only need
// to be unique within this segmenter instance.
constexpr int kNumChunksParamId     = 0;
constexpr int kChunkEltsParamIdBase = 1;

void validateChunksCoverInput(
        std::initializer_list<ChunkSpec> chunks,
        const openzl::Input& input)
{
    size_t plannedElts = 0;
    for (const ChunkSpec& chunk : chunks) {
        if (chunk.numElts > input.numElts() - plannedElts) {
            throw openzl::Exception(
                    "makeMultiChunkFrame: chunk plan exceeds input element count");
        }
        plannedElts += chunk.numElts;
    }
    if (plannedElts != input.numElts()) {
        throw openzl::Exception(
                "makeMultiChunkFrame: chunk plan does not cover input");
    }
}

size_t unguardedCompressBound(const openzl::Input& input)
{
    size_t inputSize = input.contentSize();
    if (input.type() == openzl::Type::String) {
        inputSize += input.numElts() * sizeof(uint32_t);
    }
    return ZL_COMPRESSBOUND_UNGUARDED(inputSize);
}

// Custom Segmenter callback: cuts one chunk per entry in the plan, routing
// chunk i to the i-th custom graph. Modeled on the built-in SEGM_serial, but
// varies the successor graph (and chunk size) per chunk instead of using one
// fixed head graph for uniform-size chunks. Reads the plan from local int
// params (chunk count + per-chunk element counts) and the per-chunk graphs from
// the custom graph list (graph i == chunk i). Defensive ZL_ERR_IF_* checks
// (rather than asserts) guard the param-derived values, per the openzl
// externally-reachable-path convention.
ZL_Report multiChunkSegmenterFn(ZL_Segmenter* sctx)
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(sctx);

    const size_t numInputs = ZL_Segmenter_numInputs(sctx);
    ZL_ERR_IF_NE(numInputs, (size_t)1, parameter_invalid);

    // Chunking requires a wire format that can encode segmented output.
    ZL_ERR_IF_LT(
            ZL_Segmenter_getCParam(sctx, ZL_CParam_formatVersion),
            ZL_CHUNK_VERSION_MIN,
            formatVersion_unsupported);

    const ZL_IntParam numChunksParam =
            ZL_Segmenter_getLocalIntParam(sctx, kNumChunksParamId);
    ZL_ERR_IF_EQ(
            numChunksParam.paramId, ZL_LP_INVALID_PARAMID, parameter_invalid);
    ZL_ERR_IF_LT(numChunksParam.paramValue, 1, parameter_invalid);
    const size_t numChunks = (size_t)numChunksParam.paramValue;

    const ZL_GraphIDList customGraphs = ZL_Segmenter_getCustomGraphs(sctx);
    ZL_ERR_IF_NE(customGraphs.nbGraphIDs, numChunks, parameter_invalid);

    for (size_t i = 0; i < numChunks; ++i) {
        const ZL_IntParam chunkEltsParam = ZL_Segmenter_getLocalIntParam(
                sctx, kChunkEltsParamIdBase + (int)i);
        ZL_ERR_IF_EQ(
                chunkEltsParam.paramId,
                ZL_LP_INVALID_PARAMID,
                parameter_invalid);
        ZL_ERR_IF_LT(chunkEltsParam.paramValue, 0, parameter_invalid);

        size_t chunkElts = (size_t)chunkEltsParam.paramValue;
        // ZL_ERR_IF_ERR's C++ result helper sets the union's _code through
        // ._error aliasing, which Infer's Pulse checker cannot model -- this is
        // a known, non-blocking false positive (T186816819, D56531383).
        // @lint-ignore INFERCPP
        ZL_ERR_IF_ERR(ZL_Segmenter_processChunk(
                sctx, &chunkElts, 1, customGraphs.graphids[i], nullptr));
    }

    return ZL_returnSuccess();
}

// Registers a Segmenter on `compressor` that realizes `chunks` against `input`:
// the distinct per-chunk graphs become the segmenter's custom graphs (in chunk
// order), and the per-chunk element counts are passed as local int params.
GraphID registerMultiChunkSegmenter(
        openzl::Compressor& compressor,
        std::initializer_list<ChunkSpec> chunks,
        const openzl::Input& input)
{
    validateChunksCoverInput(chunks, input);

    const size_t numChunks = chunks.size();

    std::vector<ZL_GraphID> graphs;
    graphs.reserve(numChunks);

    std::vector<ZL_IntParam> intParams;
    intParams.reserve(numChunks + 1);
    intParams.push_back(ZL_IntParam{ kNumChunksParamId, (int)numChunks });

    int chunkIdx = 0;
    for (const ChunkSpec& chunk : chunks) {
        graphs.push_back(chunk.graph);
        intParams.push_back(
                ZL_IntParam{ kChunkEltsParamIdBase + chunkIdx,
                             (int)chunk.numElts });
        ++chunkIdx;
    }

    // OpenZL copies customGraphs and localParams during registration, so the
    // local vectors above only need to outlive this call.
    ZL_LocalParams localParams        = {};
    localParams.intParams.intParams   = intParams.data();
    localParams.intParams.nbIntParams = intParams.size();

    ZL_SegmenterDesc desc       = {};
    desc.name                   = "gpu_testkit_multichunk";
    desc.segmenterFn            = multiChunkSegmenterFn;
    const ZL_Type inputTypeMask = ZL_Input_type(input.get());
    desc.inputTypeMasks         = &inputTypeMask;
    desc.numInputs              = 1;
    desc.lastInputIsVariable    = false;
    desc.customGraphs           = graphs.data();
    desc.numCustomGraphs        = graphs.size();
    desc.localParams            = localParams;

    const GraphID segmenter =
            ZL_Compressor_registerSegmenter(compressor.get(), &desc);
    if (!ZL_GraphID_isValid(segmenter)) {
        throw openzl::Exception(
                "makeMultiChunkFrame: failed to register segmenter");
    }
    return segmenter;
}

void applyFidelityParams(openzl::CCtx& cctx)
{
    cctx.setParameter(openzl::CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    cctx.setParameter(
            openzl::CParam::StoreOnExpansion, ZL_TernaryParam_disable);
    cctx.setParameter(openzl::CParam::MinStreamSize, kDisableMinStreamSize);
}

} // namespace

std::string makeMultiChunkFrame(
        openzl::Compressor& compressor,
        std::initializer_list<ChunkSpec> chunks,
        const openzl::Input& input)
{
    const GraphID segmenter =
            registerMultiChunkSegmenter(compressor, chunks, input);
    compressor.selectStartingGraph(segmenter);

    openzl::CCtx cctx;
    cctx.refCompressor(compressor);
    applyFidelityParams(cctx);
    std::string frame(unguardedCompressBound(input), '\0');
    frame.resize(
            cctx.compress(frame, poly::span<const openzl::Input>(&input, 1)));
    return frame;
}

openzl::Input
sliceInput(const openzl::Input& input, size_t eltOffset, size_t numElts)
{
    if (eltOffset > input.numElts() || numElts > input.numElts() - eltOffset) {
        throw openzl::Exception(
                "sliceInput: slice exceeds input element count");
    }
    const size_t eltWidth = input.eltWidth();
    const char* const base =
            static_cast<const char*>(input.ptr()) + eltOffset * eltWidth;

    switch (input.type()) {
        case openzl::Type::Numeric:
            return openzl::Input::refNumeric(base, eltWidth, numElts);
        case openzl::Type::Serial:
            // Serial inputs are byte streams: one element == one byte.
            return openzl::Input::refSerial(base, numElts);
        case openzl::Type::Struct:
        case openzl::Type::String:
            break;
    }
    throw openzl::Exception(
            "sliceInput: only Serial and Numeric inputs are supported");
}

} // namespace openzl::gpu::testkit
