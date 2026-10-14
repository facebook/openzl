// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include "security/lionhead/utils/lib_ftest/fdp/fdp/fdp_impl.h"
#include "security/lionhead/utils/lib_ftest/ftest.h"

#include "custom_parsers/safetensors/safetensors_parser.h"
#include "custom_parsers/safetensors/tests/safetensors_builder.h"
#include "openzl/openzl.hpp"
#include "tests/datagen/random_producer/LionheadFDPWrapper.h"

using namespace ::testing;
using namespace facebook::security::lionhead::fdp;
using openzl::tests::datagen::LionheadFDPWrapper;

namespace openzl::custom_parsers {

class SafetensorsTest : public ::testing::Test {
   protected:
    void roundtrip(std::string_view input, size_t chunkSize)
    {
        const auto gid =
                ZL_Safetensors_registerGraph(compressor_.get(), chunkSize);
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

FUZZ_F(SafetensorsTest, RandomInputFuzzer)
{
    auto rw              = rwFromFDP(f);
    const auto chunkSize = rw.u32_range("chunkSize", 64, 1 << 20);
    const auto input     = rw.all_remaining_bytes();
    roundtrip(
            std::string_view((const char*)input.data(), input.size()),
            chunkSize);
}

/// Mostly valid files, to exercise segmentation and chunking.
FUZZ_F(SafetensorsTest, StructuredInputFuzzer)
{
    static const std::vector<std::pair<std::string, size_t>> kDtypes = {
        { "BF16", 2 }, { "F16", 2 },  { "F32", 4 }, { "F64", 8 },
        { "I64", 8 },  { "BOOL", 1 }, { "U16", 2 }, { "F4", 1 },
    };
    auto rw = rwFromFDP(f);
    const auto chunkSize =
            rw.u32_range("chunkSize", 2 * ZL_MIN_CHUNK_SIZE, 1 << 18);
    const auto nbTensors = rw.usize_range("nbTensors", 0, 12);
    std::vector<testing::TensorSpec> tensors;
    for (size_t i = 0; i < nbTensors; ++i) {
        const auto& [dtype, eltSize] =
                kDtypes[rw.usize_range("dtype", 0, kDtypes.size() - 1)];
        std::vector<uint64_t> shape(rw.usize_range("ndims", 0, 3));
        size_t nbElts = 1;
        for (auto& dim : shape) {
            dim = rw.usize_range("dim", 0, 40);
            nbElts *= dim;
        }
        const size_t mismatch = rw.usize_range("mismatch", 0, 7) == 0 ? 1 : 0;
        tensors.push_back(
                {
                        "layers."
                                + std::to_string(rw.usize_range("layer", 0, 3))
                                + ".t" + std::to_string(i),
                        dtype,
                        shape,
                        rw.usize_range("constant", 0, 3) == 0
                                ? std::string(
                                          nbElts * eltSize + mismatch,
                                          (char)rw.u8("byte"))
                                : testing::pseudoRandomBytes(
                                          nbElts * eltSize + mismatch,
                                          rw.u32("seed")),
                        rw.usize_range("gap", 0, 3) == 0
                                ? rw.usize_range("gapSize", 1, 16)
                                : 0,
                });
    }
    auto file = testing::buildSafetensors(tensors);
    if (rw.usize_range("corrupt", 0, 3) == 0) {
        file[rw.usize_range("corruptPos", 0, file.size() - 1)] ^=
                (char)rw.u8_range("corruptBits", 1, 255);
    }
    roundtrip(file, chunkSize);
}

} // namespace openzl::custom_parsers
