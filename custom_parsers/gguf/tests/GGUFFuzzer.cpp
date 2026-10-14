// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "security/lionhead/utils/lib_ftest/fdp/fdp/fdp_impl.h"
#include "security/lionhead/utils/lib_ftest/ftest.h"

#include "custom_parsers/gguf/gguf_lexer.h"
#include "custom_parsers/gguf/gguf_parser.h"
#include "custom_parsers/gguf/tests/gguf_builder.h"
#include "openzl/openzl.hpp"
#include "tests/datagen/random_producer/LionheadFDPWrapper.h"

using namespace ::testing;
using namespace facebook::security::lionhead::fdp;
using openzl::tests::datagen::LionheadFDPWrapper;

namespace openzl::custom_parsers {

class GGUFTest : public ::testing::Test {
   protected:
    void roundtrip(std::string_view input, size_t chunkSize)
    {
        const auto gid = ZL_GGUF_registerGraph(compressor_.get(), chunkSize);
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

FUZZ_F(GGUFTest, RandomInputFuzzer)
{
    auto rw              = rwFromFDP(f);
    const auto chunkSize = rw.u32_range("chunkSize", 64, 1 << 20);
    const auto input     = rw.all_remaining_bytes();
    roundtrip(
            std::string_view((const char*)input.data(), input.size()),
            chunkSize);
}

/// Mostly valid files, to exercise segmentation and chunking.
FUZZ_F(GGUFTest, StructuredInputFuzzer)
{
    static const std::vector<uint32_t> kTypes = {
        ZL_GGUFType_f32,  ZL_GGUFType_f16,    ZL_GGUFType_bf16,
        ZL_GGUFType_i32,  ZL_GGUFType_q4_0,   ZL_GGUFType_q5_1,
        ZL_GGUFType_q8_0, ZL_GGUFType_q3_k,   ZL_GGUFType_q4_k,
        ZL_GGUFType_q6_k, ZL_GGUFType_iq2_xs, 99,
    };
    auto rw = rwFromFDP(f);
    const auto chunkSize =
            rw.u32_range("chunkSize", 2 * ZL_MIN_CHUNK_SIZE, 1 << 18);
    const auto alignment = rw.usize_range("alignment", 0, 3) == 0 ? 64 : 32;
    const auto nbTensors = rw.usize_range("nbTensors", 0, 12);
    std::vector<testing::GGUFTensorSpec> tensors;
    for (size_t i = 0; i < nbTensors; ++i) {
        const uint32_t type =
                kTypes[rw.usize_range("type", 0, kTypes.size() - 1)];
        size_t blockElts = 1, blockBytes = 1;
        ZL_GGUF_typeLayout(type, &blockElts, &blockBytes);
        // Rows of whole blocks, except sometimes.
        const size_t rowElts = rw.usize_range("partial", 0, 7) == 0
                ? rw.usize_range("rowElts", 1, 300)
                : blockElts * rw.usize_range("rowBlocks", 0, 4);
        const size_t nbRows  = rw.usize_range("nbRows", 1, 40);
        const size_t nbBytes = rowElts * nbRows / blockElts * blockBytes;
        tensors.push_back(
                {
                        "blk." + std::to_string(rw.usize_range("layer", 0, 3))
                                + ".t" + std::to_string(i),
                        type,
                        { rowElts, nbRows },
                        rw.usize_range("constant", 0, 3) == 0
                                ? std::string(nbBytes, (char)rw.u8("byte"))
                                : testing::pseudoRandomBytes(
                                          nbBytes, rw.u32("seed")),
                });
    }
    auto file = testing::buildGGUF(tensors, alignment);
    if (rw.usize_range("corrupt", 0, 3) == 0) {
        file[rw.usize_range("corruptPos", 0, file.size() - 1)] ^=
                (char)rw.u8_range("corruptBits", 1, 255);
    }
    roundtrip(file, chunkSize);
}

} // namespace openzl::custom_parsers
