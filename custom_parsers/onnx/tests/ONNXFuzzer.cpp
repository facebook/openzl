// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <utility>
#include "security/lionhead/utils/lib_ftest/fdp/fdp/fdp_impl.h"
#include "security/lionhead/utils/lib_ftest/ftest.h"

#include "custom_parsers/onnx/onnx_lexer.h"
#include "custom_parsers/onnx/onnx_parser.h"
#include "custom_parsers/onnx/tests/onnx_builder.h"
#include "openzl/openzl.hpp"
#include "tests/datagen/random_producer/LionheadFDPWrapper.h"

using namespace ::testing;
using namespace facebook::security::lionhead::fdp;
using openzl::tests::datagen::LionheadFDPWrapper;

namespace openzl::custom_parsers {

class ONNXTest : public ::testing::Test {
   protected:
    void roundtrip(std::string_view input, size_t chunkSize)
    {
        const auto gid = ZL_ONNX_registerGraph(compressor_.get(), chunkSize);
        ASSERT_FALSE(ZL_RES_isError(gid));
        compressor_.selectStartingGraph(ZL_RES_value(gid));
        cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        cctx_.refCompressor(compressor_);
        const auto compressed = cctx_.compressSerial(input);
        ASSERT_EQ(dctx_.decompressSerial(compressed), input);
    }

    CCtx cctx_{};
    DCtx dctx_{};
    Compressor compressor_{};
};

template <class HarnessMode>
LionheadFDPWrapper<StructuredFDP<HarnessMode>> rwFromFDP(
        StructuredFDP<HarnessMode>& fdp)
{
    return LionheadFDPWrapper<StructuredFDP<HarnessMode>>(fdp);
}

FUZZ_F(ONNXTest, RandomInputFuzzer)
{
    auto rw              = rwFromFDP(f);
    const auto chunkSize = rw.u32_range("chunkSize", 64, 1 << 20);
    const auto input     = rw.all_remaining_bytes();
    roundtrip(
            std::string_view((const char*)input.data(), input.size()),
            chunkSize);
}

/// Graphs stop growing once their tensors hold this many bytes, so that runs
/// stay fast.
constexpr size_t kMaxTotalBytes = (size_t)4 << 20;

/// A tensor of any data type, including unknown ones, whose raw data mostly
/// matches its dims. Its size is added to @p totalBytes.
template <class RW>
testing::ONNXTensorSpec randomTensor(RW& rw, size_t i, size_t& totalBytes)
{
    const uint32_t dataType = rw.u32_range("dataType", 0, 28);
    std::vector<uint64_t> dims(rw.usize_range("ndims", 0, 3));
    size_t nbElts = 1;
    for (auto& dim : dims) {
        dim = rw.usize_range("dim", 0, 40);
        nbElts *= dim;
    }
    const size_t bits = ZL_ONNX_dataTypeBits(dataType);
    size_t nbBytes    = (nbElts * (bits ? bits : 8) + 7) / 8;
    if (rw.usize_range("inconsistent", 0, 7) == 0) {
        nbBytes = rw.usize_range("nbBytes", 0, 300);
    }
    totalBytes += nbBytes;
    return { "t" + std::to_string(i),
             dataType,
             std::move(dims),
             rw.usize_range("constant", 0, 3) == 0
                     ? std::string(nbBytes, (char)rw.u8("byte"))
                     : testing::pseudoRandomBytes(nbBytes, rw.u32("seed")) };
}

/// A graph of random initializers and Constant nodes, with If nodes nesting
/// subgraphs up to one level past the depth the lexer walks.
template <class RW>
std::string
randomGraph(RW& rw, int depth, size_t& nbTensors, size_t& totalBytes)
{
    std::vector<std::string> nodes;
    std::vector<testing::ONNXTensorSpec> initializers;
    const size_t nbItems = rw.usize_range("nbItems", 0, depth == 0 ? 12 : 3);
    for (size_t i = 0; i < nbItems && totalBytes < kMaxTotalBytes; ++i) {
        switch (rw.usize_range("item", 0, 3)) {
            case 0:
                nodes.push_back(
                        testing::constantNode(
                                randomTensor(rw, nbTensors++, totalBytes)));
                break;
            case 1:
                if (depth <= ZL_ONNX_MAX_GRAPH_DEPTH) {
                    nodes.push_back(
                            testing::ifNode(randomGraph(
                                    rw, depth + 1, nbTensors, totalBytes)));
                }
                break;
            default:
                initializers.push_back(
                        randomTensor(rw, nbTensors++, totalBytes));
                break;
        }
    }
    return testing::graphProto(nodes, initializers);
}

/// Mostly valid files, to exercise segmentation and chunking.
FUZZ_F(ONNXTest, StructuredInputFuzzer)
{
    auto rw = rwFromFDP(f);
    const auto chunkSize =
            rw.u32_range("chunkSize", 2 * ZL_MIN_CHUNK_SIZE, 1 << 18);
    size_t nbTensors  = 0;
    size_t totalBytes = 0;
    auto file = testing::buildONNX(randomGraph(rw, 0, nbTensors, totalBytes));
    if (rw.usize_range("corrupt", 0, 3) == 0) {
        file[rw.usize_range("corruptPos", 0, file.size() - 1)] ^=
                (char)rw.u8_range("corruptBits", 1, 255);
    }
    roundtrip(file, chunkSize);
}

} // namespace openzl::custom_parsers
