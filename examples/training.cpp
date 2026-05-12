// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <chrono>
#include <fstream>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>
#include "openzl/codecs/zl_clustering.h"
#include "openzl/codecs/zl_conversion.h"
#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/zl_compressor.h"
#include "openzl/zl_graph_api.h"
#include "tools/io/InputFile.h"
#include "tools/io/InputSetBuilder.h"
#include "tools/training/train.h"
#include "tools/training/utils/utils.h"

#include "examples/example_utils.h"
#include "openzl/common/logging.h"
#include "openzl/shared/mem.h" // Cheat with a private header for ZL_readLE32
#include "openzl/zl_errors.h"

namespace openzl::examples {

static uint32_t readLE32(const void* ptr)
{
    return ZL_readLE32(ptr);
}

// --8<-- [start:get-data]
static ZL_Report parsingCompressorGraphFn(
        ZL_Graph* graph,
        ZL_Edge* inputEdges[],
        size_t numInputs) noexcept
{
    // Sets up the error context for rich error messages.
    ZL_RESULT_DECLARE_SCOPE_REPORT(graph);

    assert(numInputs == 1);
    const ZL_Input* const input    = ZL_Edge_getData(inputEdges[0]);
    const uint8_t* const inputData = (const uint8_t*)ZL_Input_ptr(input);
    const size_t inputSize         = ZL_Input_numElts(input);
    // --8<-- [end:get-data]

    std::vector<unsigned> dispatchIdxs;
    std::vector<size_t> sizes;
    // Field used for node conversion
    std::vector<uint8_t> eltWidths;

    // --8<-- [start:tag-defs]
    std::unordered_map<uint32_t, uint32_t> tagToDispatchIdx;
    // Reverse mapping from dispatchIdxs to tag
    std::unordered_map<uint32_t, uint32_t> dispatchIdxToTag;
    // Current dispatch index
    uint32_t currentDispatchIdx     = 0;
    constexpr unsigned kNumBytesTag = 100;
    constexpr unsigned kEltWidthTag = 101;
    constexpr unsigned kInputTag    = 102;

    dispatchIdxToTag[currentDispatchIdx] = kNumBytesTag;
    tagToDispatchIdx[kNumBytesTag]       = currentDispatchIdx++;

    dispatchIdxToTag[currentDispatchIdx] = kEltWidthTag;
    tagToDispatchIdx[kEltWidthTag]       = currentDispatchIdx++;

    dispatchIdxToTag[currentDispatchIdx] = kInputTag;
    tagToDispatchIdx[kInputTag]          = currentDispatchIdx++;
    // A tag that tracks the current dispatch index

    // --8<-- [end:tag-defs]

    // The format is:
    //
    // [4-byte little-endian num-bytes]
    // [1-byte element-width]
    // [4-byte input tag]
    // [(num-bytes)-byte data]
    // ...
    //
    // We're going to separate the number of elements and element width fields
    // into their own outputs. Then we'll separate the numeric data into streams
    // based on the element width.
    // --8<-- [start:lex]
    for (size_t inputPos = 0; inputPos < inputSize;) {
        // Return an error if there isn't enough bytes for a header.
        ZL_ERR_IF_LT(inputSize - inputPos, 5, srcSize_tooSmall);

        const uint32_t numBytes = readLE32(inputData + inputPos);
        const uint8_t eltWidth  = inputData[inputPos + 4];
        const uint32_t inputTag = readLE32(inputData + inputPos + 5);

        // Increment dispatch index if there is an unseen tag
        if (tagToDispatchIdx.count(inputTag) == 0) {
            dispatchIdxToTag[currentDispatchIdx] = inputTag;
            tagToDispatchIdx[inputTag]           = currentDispatchIdx++;
        }

        eltWidths.push_back(eltWidth);

        ZL_ERR_IF_NE(numBytes % eltWidth, 0, corruption);
        inputPos += 9;

        ZL_ERR_IF_LT(inputSize - inputPos, numBytes, srcSize_tooSmall);
        inputPos += numBytes;

        dispatchIdxs.push_back(tagToDispatchIdx[kNumBytesTag]);
        sizes.push_back(4);

        dispatchIdxs.push_back(tagToDispatchIdx[kEltWidthTag]);
        sizes.push_back(1);

        dispatchIdxs.push_back(tagToDispatchIdx[kInputTag]);
        sizes.push_back(4);

        dispatchIdxs.push_back(tagToDispatchIdx[inputTag]);
        sizes.push_back(numBytes);
    }
    // --8<-- [end:lex]
    // Dispatch each field of the input based on the tag & the size.
    // We'll end up with numTags + 2 output streams.
    // The first output stream is the tags stream (content of tags vector).
    // The second output stream is the sizes stream (content of sizes vector).
    // Then there is an output stream for each tag.
    // --8<-- [start:dispatch]
    const ZL_DispatchInstructions instructions = {
        .segmentSizes = sizes.data(),
        .tags         = dispatchIdxs.data(),
        .nbSegments   = sizes.size(),
        .nbTags       = currentDispatchIdx,
    };
    ZL_TRY_LET(
            ZL_EdgeList,
            dispatchEdges,
            ZL_Edge_runDispatchNode(inputEdges[0], &instructions));
    assert(dispatchEdges.nbEdges == 2 + currentDispatchIdx);
    // --8<-- [end:dispatch]
    // A list to store all output edges without a destination
    std::vector<ZL_Edge*> outputEdges;
    outputEdges.reserve(dispatchEdges.nbEdges);
    // --8<-- [start:metadata]
    // Send tags and sizes to compress generic
    ZL_ERR_IF_ERR(ZL_Edge_setDestination(
            dispatchEdges.edges[0], ZL_GRAPH_COMPRESS_GENERIC));
    ZL_ERR_IF_ERR(ZL_Edge_setDestination(
            dispatchEdges.edges[1], ZL_GRAPH_COMPRESS_GENERIC));
    dispatchEdges.edges += 2;

    // The outputs with indices [0, 5) are for the tags, sizes and the 3
    // metadata fields. Set the metadata according to the mapping
    for (size_t i = 0; i < 3; i++) {
        ZL_ERR_IF_ERR(ZL_Edge_setIntMetadata(
                dispatchEdges.edges[i],
                ZL_CLUSTERING_TAG_METADATA_ID,
                dispatchIdxToTag[i]));
        outputEdges.push_back(dispatchEdges.edges[i]);
    }
    // --8<-- [end:metadata]

    // Convert the serial streams to numeric streams as specified by the format
    assert(eltWidths.size() == dispatchEdges.nbEdges - 5);

    // --8<-- [start:conversion]
    for (size_t i = 0; i < eltWidths.size(); i++) {
        // Creates a node that interprets serial data as little-endian numeric
        // and converts to the specified eltWidth output
        ZL_NodeID node   = ZL_Node_interpretAsLE(eltWidths[i] * 8);
        auto dispatchIdx = i + 3;
        ZL_TRY_LET_CONST(
                ZL_EdgeList,
                convertEdges,
                ZL_Edge_runNode(dispatchEdges.edges[dispatchIdx], node));
        assert(convertEdges.nbEdges == 1);
        // The input format specifies that the input tag is written to the 4
        // bytes specified. Assigns tag accordingly
        ZL_ERR_IF_ERR(ZL_Edge_setIntMetadata(
                convertEdges.edges[0],
                ZL_CLUSTERING_TAG_METADATA_ID,
                dispatchIdxToTag[dispatchIdx]));
        outputEdges.push_back(convertEdges.edges[0]);
    }
    // --8<-- [end:conversion]

    // --8<-- [start:custom-graphs]
    // Get the custom graphs
    ZL_GraphIDList customGraphs = ZL_Graph_getCustomGraphs(graph);
    // Expect there to be one custom graph to handle the outputs produced by the
    // parser
    ZL_ERR_IF_NE(customGraphs.nbGraphIDs, 1, graphParameter_invalid);
    // Expect the number of output edges are equal to the output edges produced
    // by dispatch as conversion is single input single output
    assert(outputEdges.size() == dispatchEdges.nbEdges - 2);
    // Send to custom graph chosen. The clustering graph is the intended
    // destination to be chosen here.
    ZL_ERR_IF_ERR(ZL_Edge_setParameterizedDestination(
            outputEdges.data(),
            outputEdges.size(),
            customGraphs.graphids[0],
            NULL));
    // --8<-- [end:custom-graphs]
    return ZL_returnSuccess();
}

} // namespace openzl::examples

static ZL_GraphID registerParsingCompressorGraph(
        openzl::Compressor& compressor,
        const ZL_GraphID clusteringGraph)
{
    // --8<-- [start:register-parser-base]
    auto parsingCompressorGraph = compressor.getGraph("Parsing Compressor");
    if (!parsingCompressorGraph) {
        ZL_Type inputTypeMask                  = ZL_Type_serial;
        ZL_FunctionGraphDesc parsingCompressor = {
            .name           = "!Parsing Compressor",
            .graph_f        = openzl::examples::parsingCompressorGraphFn,
            .inputTypeMasks = &inputTypeMask,
            .nbInputs       = 1,
            .customGraphs   = NULL,
            .nbCustomGraphs = 0,
            .localParams    = {},
        };
        parsingCompressorGraph =
                compressor.registerFunctionGraph(parsingCompressor);
    }
    // --8<-- [end:register-parser-base]
    // --8<-- [start:register-parser-parameterize]
    std::vector<ZL_GraphID> customGraphs                 = { clusteringGraph };
    openzl::GraphParameters parsingCompressorGraphParams = {
        .customGraphs = std::move(customGraphs),
    };
    parsingCompressorGraph = compressor.parameterizeGraph(
            parsingCompressorGraph.value(), parsingCompressorGraphParams);
    // --8<-- [end:register-parser-parameterize]
    return parsingCompressorGraph.value();
}

// --8<-- [start:register-clustering]
static ZL_GraphID registerGraph_ParsingCompressor(
        openzl::Compressor& compressor)
{
    /* Use an empty default config if we don't know
     * what data we are working with. nbClusters and nbTypeDefaults must be 0 if
     * pointers are uninitialized. */
    ZL_ClusteringConfig defaultConfig{
        .nbClusters     = 0,
        .nbTypeDefaults = 0,
    };

    /* A set of successors we expect may be useful for our data set. */
    std::vector<ZL_GraphID> successors = {
        ZL_GRAPH_STORE,
        ZL_GRAPH_ZSTD,
        ZL_GRAPH_COMPRESS_GENERIC,
        ZL_Compressor_registerStaticGraph_fromNode1o(
                compressor.get(), ZL_NODE_DELTA_INT, ZL_GRAPH_FIELD_LZ),
    };
    /* Create the clustering graph */
    ZL_GraphID clusteringGraph = ZL_Clustering_registerGraph(
            compressor.get(),
            &defaultConfig,
            successors.data(),
            successors.size());

    return registerParsingCompressorGraph(compressor, clusteringGraph);
}
// --8<-- [end:register-clustering]

// --8<-- [start:compressor-creation]
static std::unique_ptr<openzl::Compressor> createCompressorFromSerialized(
        openzl::poly::string_view serialized)
{
    auto compressor = std::make_unique<openzl::Compressor>();
    registerGraph_ParsingCompressor(*compressor);
    compressor->deserialize(serialized);
    return compressor;
}
// --8<-- [end:compressor-creation]

static void train_example(
        const std::string& inputDir,
        const std::string& outputPath)
{
    openzl::tools::io::InputSetBuilder builder(true);
    auto inputs =
            openzl::tools::io::InputSetBuilder(true).add_path(inputDir).build();
    // --8<-- [start:train]
    openzl::Compressor compressor;
    auto graphId = registerGraph_ParsingCompressor(compressor);
    openzl::unwrap(
            ZL_Compressor_selectStartingGraphID(compressor.get(), graphId),
            "Failed to select starting graph ID",
            compressor.get());

    openzl::training::TrainParams trainParams = {
        .compressorGenFunc = createCompressorFromSerialized,
        .threads           = 1,
        .clusteringTrainer = openzl::training::ClusteringTrainer::Greedy,
    };
    auto multiInputs = openzl::training::inputSetToMultiInputs(*inputs);
    auto serialized =
            openzl::training::train(multiInputs, compressor, trainParams)[0];
    // --8<-- [end:train]

    // --8<-- [start:compress]
    openzl::CCtx cctx;
    std::string out;
    auto testCompressor =
            createCompressorFromSerialized(serialized.serializedCompressor);
    // Try compressing every file provided
    for (const auto& inputPtr : *inputs) {
        cctx.setParameter(openzl::CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        cctx.refCompressor(*testCompressor);
        // Allocate size for output buffer
        out.resize(openzl::compressBound(inputPtr->contents().size()));
        auto csize = cctx.compressSerial(out, inputPtr->contents());
        std::cerr << "Compressed " << inputPtr->contents().size()
                  << " bytes to " << csize << std::endl;
    }
    // --8<-- [end:compress]

    // Saves a compressor to the designated path
    std::ofstream output(outputPath);
    if (!output) {
        std::cerr << "Error opening file for writing: " << outputPath
                  << std::endl;
    }
    output << serialized.serializedCompressor;
    output.close();
}

static void test_example(
        const std::string& inputDir,
        const std::string& compressorPath)
{
    openzl::tools::io::InputSetBuilder builder(true);
    auto inputs =
            openzl::tools::io::InputSetBuilder(true).add_path(inputDir).build();
    openzl::tools::io::InputFile compressorFile(compressorPath);
    // --8<-- [start:deserialize]
    openzl::Compressor compressor;
    // Register dependencies
    registerGraph_ParsingCompressor(compressor);
    // Deserialize the compressor
    compressor.deserialize(compressorFile.contents());
    // --8<-- [end:deserialize]

    // Statistics
    size_t totalCompressedSize   = 0;
    size_t totalUncompressedSize = 0;
    size_t cTimeUs               = 0;
    size_t dTimeUs               = 0;

    // Benchmark compressions
    for (auto& inputPtr : *inputs) {
        std::string out;
        openzl::CCtx cctx;
        cctx.refCompressor(compressor);
        cctx.setParameter(openzl::CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        const auto& contents        = inputPtr->contents();
        const auto uncompressedSize = contents.size();
        out.resize(openzl::compressBound(contents.size()));
        auto start = std::chrono::high_resolution_clock::now();
        auto csize = cctx.compressSerial(out, contents);
        auto end   = std::chrono::high_resolution_clock::now();
        out.resize(csize);
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end - start);
        cTimeUs += duration.count();

        // Do a decompression to benchmark decompression and ensure data round
        // trips

        start = std::chrono::high_resolution_clock::now();
        // --8<-- [start:decompression]
        openzl::DCtx dctx;
        auto regen = dctx.decompressSerial(out);
        // --8<-- [end:decompression]
        end      = std::chrono::high_resolution_clock::now();
        duration = std::chrono::duration_cast<std::chrono::microseconds>(
                end - start);
        dTimeUs += duration.count();
        if (regen != contents) {
            throw std::runtime_error(
                    "Data mismatch! Compression did not round trip.");
        }
        totalCompressedSize += csize;
        totalUncompressedSize += uncompressedSize;
    }
    auto cMbps            = totalUncompressedSize / (double)cTimeUs;
    auto compressionRatio = totalUncompressedSize / (double)totalCompressedSize;
    auto dMbps            = totalUncompressedSize / (double)dTimeUs;
    // Print out the statistics
    fprintf(stderr,
            "Compressed %zu bytes to %zu bytes(cMbps: %.2f MB/s, dMbps %.2f MB/s, %.2fx)\n",
            totalUncompressedSize,
            totalCompressedSize,
            cMbps,
            dMbps,
            compressionRatio);
}

int main(int argc, char** argv)
{
    // Use debug log level
    ZL_g_logLevel = ZL_LOG_LVL_DEBUG;
    {
        if (argc != 4) {
            fprintf(stderr,
                    "Usage: %s <train|test> <input folder> <output path>\n",
                    argv[0]);
            fprintf(stderr,
                    "\tTrains data in the format: [32-bit num-bytes][8-bit element-width][num-bytes data]...");
            return 1;
        }
    }
    const std::string command    = argv[1];
    const std::string inputDir   = argv[2];
    const std::string outputPath = argv[3];
    if (command == "train") {
        train_example(inputDir, outputPath);
    } else if (command == "test") {
        test_example(inputDir, outputPath);
    }
    return 0;
}
