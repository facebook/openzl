// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>
#include <numeric>
#include <utility>
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

/// Writes a metadata value of @p type at @p depth, with arrays nested one level
/// deeper than the lexer accepts. Types past 12 are invalid and have no value.
template <class RW>
void writeRandomValue(RW& rw, testing::GGUFWriter& w, uint32_t type, int depth)
{
    // sizes of the scalar types, 0 for strings and arrays
    static const size_t kScalarSizes[] = {
        1, 1, 2, 2, 4, 4, 4, 1, 0, 0, 8, 8, 8
    };
    if (type == 8) {
        w.str(std::string(rw.usize_range("strSize", 0, 8), 's'));
    } else if (type == 9) {
        const uint32_t eltType = rw.u32_range("eltType", 0, depth < 8 ? 13 : 8);
        const size_t count     = rw.usize_range("count", 0, 2);
        w.u32(eltType);
        w.u64(count);
        for (size_t i = 0; i < count; ++i) {
            writeRandomValue(rw, w, eltType, depth + 1);
        }
    } else if (type < 13) {
        w.out.append(kScalarSizes[type], (char)rw.u8("scalar"));
    }
}

/// Metadata of random types and values, among which general.alignment, maybe
/// repeated, of another type, or past the end of the file.
template <class RW>
testing::GGUFMetadata randomMetadata(RW& rw)
{
    testing::GGUFMetadata metadata{ rw.usize_range("nbMetadata", 0, 6), "" };
    testing::GGUFWriter w;
    for (uint64_t i = 0; i < metadata.count; ++i) {
        const bool isAlignment = rw.usize_range("isAlignment", 0, 3) == 0;
        w.str(isAlignment ? "general.alignment" : "k" + std::to_string(i));
        const uint32_t type =
                isAlignment && rw.usize_range("alignmentType", 0, 3) != 0
                ? 4 // u32
                : rw.u32_range("valueType", 0, 13);
        w.u32(type);
        if (isAlignment && type == 4) {
            w.u32(rw.usize_range("powerOf2", 0, 3) != 0
                          ? 1u << rw.u32_range("alignmentLog", 0, 31)
                          : rw.u32("alignment"));
        } else {
            writeRandomValue(rw, w, type, 0);
        }
    }
    metadata.bytes = std::move(w.out);
    return metadata;
}

/// Mostly valid files, to exercise segmentation and chunking.
FUZZ_F(GGUFTest, StructuredInputFuzzer)
{
    auto rw = rwFromFDP(f);
    const auto chunkSize =
            rw.u32_range("chunkSize", 2 * ZL_MIN_CHUNK_SIZE, 1 << 18);
    const auto alignment = rw.usize_range("alignment", 0, 3) == 0 ? 64 : 32;
    const auto nbTensors = rw.usize_range("nbTensors", 0, 12);
    std::vector<testing::GGUFTensorSpec> tensors;
    for (size_t i = 0; i < nbTensors; ++i) {
        // Unassigned values are types of unknown size.
        const uint32_t type = rw.u32_range("type", 0, ZL_GGUFType_limit);
        size_t blockElts = 1, blockBytes = 1;
        ZL_GGUF_typeLayout(type, &blockElts, &blockBytes);
        // Up to 4 dimensions, with rows of whole blocks, except sometimes.
        const size_t rowElts     = rw.usize_range("partial", 0, 7) == 0
                    ? rw.usize_range("rowElts", 1, 300)
                    : blockElts * rw.usize_range("rowBlocks", 0, 4);
        std::vector<uint64_t> ne = { rowElts,
                                     rw.usize_range("nbRows", 1, 40),
                                     rw.usize_range("ne2", 1, 2),
                                     rw.usize_range("ne3", 1, 2) };
        ne.resize(rw.usize_range("ndims", 0, 4));
        size_t nbElts = 1;
        for (const auto dim : ne) {
            nbElts *= dim;
        }
        const size_t nbBytes = nbElts / blockElts * blockBytes;
        tensors.push_back(
                {
                        "blk." + std::to_string(rw.usize_range("layer", 0, 3))
                                + ".t" + std::to_string(i),
                        type,
                        std::move(ne),
                        rw.usize_range("constant", 0, 3) == 0
                                ? std::string(nbBytes, (char)rw.u8("byte"))
                                : testing::pseudoRandomBytes(
                                          nbBytes, rw.u32("seed")),
                });
    }
    const auto metadata = rw.usize_range("randomMetadata", 0, 1) == 0
            ? testing::sampleMetadata()
            : randomMetadata(rw);
    // Tensor infos in any order: the lexer sorts tensors by position.
    std::vector<size_t> infoOrder(nbTensors);
    std::iota(infoOrder.begin(), infoOrder.end(), 0);
    for (size_t i = nbTensors; i > 1; --i) {
        std::swap(
                infoOrder[i - 1],
                infoOrder[rw.usize_range("infoSwap", 0, i - 1)]);
    }
    auto file = testing::buildGGUF(tensors, alignment, metadata, infoOrder);
    if (rw.usize_range("corrupt", 0, 3) == 0) {
        file[rw.usize_range("corruptPos", 0, file.size() - 1)] ^=
                (char)rw.u8_range("corruptBits", 1, 255);
    }
    roundtrip(file, chunkSize);
}

} // namespace openzl::custom_parsers
