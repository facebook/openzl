// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/training/utils/utils.h"
#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/Exception.hpp"
#include "openzl/zl_reflection.h"

namespace openzl::training {

CCtx refCCtxForTraining(const Compressor& compressor)
{
    openzl::CCtx cctx;
    cctx.setParameter(openzl::CParam::StickyParameters, 1);
    cctx.refCompressor(compressor);
    return cctx;
}

size_t MultiInput::compressBound() const
{
    size_t totalSrcSize = 0;
    for (const auto& input : *inputs_) {
        totalSrcSize += input.contentSize();
        if (input.type() == Type::String) {
            totalSrcSize += input.numElts() * sizeof(*input.stringLens());
        }
    }
    totalSrcSize += inputs_->size() * 256;
    return 2 * ZL_compressBound(totalSrcSize) + 1024;
}

std::vector<MultiInput> inputSetToMultiInputs(tools::io::InputSet& inputs)
{
    // Convert the io inputs to MultiInputs
    std::vector<MultiInput> multiInputs;
    for (auto& input : inputs) {
        auto mi = openzl::training::MultiInput();
        mi.add(input);
        multiInputs.push_back(std::move(mi));
    }
    return multiInputs;
}

bool compressorIsFormatCompatible(
        const Compressor& compressor,
        const std::vector<MultiInput>& inputs)
{
    CCtx cctx;
    cctx.refCompressor(compressor);
    for (const auto& input : inputs) {
        const size_t outputCapacity = input.compressBound();
        std::string output(outputCapacity, '\0');
        try {
            cctx.compress(output, *input);
        } catch (const Exception& e) {
            // Catch only format version unsupported errors. Otherwise it is
            // failing compression on the input but is actually supported format
            // version-wise.
            if (e.code() == ZL_ErrorCode_formatVersion_unsupported
                || e.code() == ZL_ErrorCode_node_versionMismatch) {
                return false;
            }
        }
    }
    return true;
}

std::vector<GraphID> filterGraphsByFormatVersion(
        Compressor& compressor,
        const std::vector<GraphID>& graphs,
        const std::vector<MultiInput>& inputs)
{
    const auto formatVersion = compressor.getParameter(CParam::FormatVersion);
    if (formatVersion < ZL_MIN_FORMAT_VERSION) {
        throw Exception("Format version is below ZL_MIN_FORMAT_VERSION");
    }
    GraphID originalStartingGraph = ZL_GRAPH_ILLEGAL;
    const bool hadStartingGraph   = ZL_Compressor_getStartingGraphID(
            compressor.get(), &originalStartingGraph);
    std::vector<GraphID> supported;
    supported.reserve(graphs.size());
    for (const auto graph : graphs) {
        compressor.selectStartingGraph(graph);
        if (compressorIsFormatCompatible(compressor, inputs)) {
            supported.push_back(graph);
        }
    }
    if (hadStartingGraph) {
        compressor.selectStartingGraph(originalStartingGraph);
    }
    return supported;
}

} // namespace openzl::training
