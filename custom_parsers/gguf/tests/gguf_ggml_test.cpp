// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "custom_parsers/gguf/gguf_lexer.h"
#include "custom_parsers/gguf/tests/gguf_builder.h"
#include "ggml.h"
#include "gguf.h"

namespace openzl::custom_parsers {
namespace {

using testing::buildGGUF;
using testing::GGUFMetadata;
using testing::GGUFTensorSpec;
using testing::GGUFWriter;
using testing::pseudoRandomBytes;

/// Where the data of a tensor lie in a file.
struct TensorPlace {
    std::string name;
    uint32_t type;
    size_t offset;
    size_t size;

    bool operator==(const TensorPlace&) const = default;
};

std::vector<TensorPlace> lexedTensors(const std::string& file)
{
    const ZL_Report maxTokens =
            ZL_GGUF_lex(file.data(), file.size(), nullptr, 0, nullptr);
    EXPECT_FALSE(ZL_isError(maxTokens));
    if (ZL_isError(maxTokens)) {
        return {};
    }
    std::vector<ZL_GGUFToken> tokens(ZL_validResult(maxTokens));
    const ZL_Report nbTokens = ZL_GGUF_lex(
            file.data(), file.size(), tokens.data(), tokens.size(), nullptr);
    EXPECT_FALSE(ZL_isError(nbTokens));
    if (ZL_isError(nbTokens)) {
        return {};
    }
    std::vector<TensorPlace> places;
    for (size_t i = 0; i < ZL_validResult(nbTokens); ++i) {
        const auto& t = tokens[i];
        if (t.type == ZL_GGUFTokenType_tensor) {
            places.push_back(
                    { std::string(t.name, t.nameSize),
                      t.ggmlType,
                      size_t(t.ptr - file.data()),
                      t.size });
        }
    }
    return places;
}

/// ggml requires tensor data in the order of their infos, which is then the
/// order of the lexed tensors.
std::vector<TensorPlace> ggmlTensors(const std::string& file)
{
    gguf_context* const ctx = gguf_init_from_buffer(
            file.data(), file.size(), { .no_alloc = true, .ctx = nullptr });
    EXPECT_NE(ctx, nullptr);
    if (ctx == nullptr) {
        return {};
    }
    const int64_t nbTensors = gguf_get_n_tensors(ctx);
    std::vector<TensorPlace> places;
    places.reserve((size_t)nbTensors);
    for (int64_t i = 0; i < nbTensors; ++i) {
        places.push_back(
                { gguf_get_tensor_name(ctx, i),
                  (uint32_t)gguf_get_tensor_type(ctx, i),
                  gguf_get_data_offset(ctx) + gguf_get_tensor_offset(ctx, i),
                  gguf_get_tensor_size(ctx, i) });
    }
    gguf_free(ctx);
    return places;
}

/// Metadata without nested arrays, which ggml does not read.
GGUFMetadata flatMetadata()
{
    GGUFWriter w;
    w.str("general.architecture");
    w.u32(8); // string
    w.str("llama");
    w.str("tokenizer.ggml.tokens");
    w.u32(9); // array
    w.u32(8); // of strings
    w.u64(2);
    w.str("a");
    w.str("bc");
    w.str("tokenizer.ggml.scores");
    w.u32(9); // array
    w.u32(6); // of f32
    w.u64(2);
    w.u32(0);
    w.u32(0);
    return { 3, w.out };
}

TEST(GGUFGgmlTest, TypeLayoutsMatchGgml)
{
    // Types that ggml added since are unknown here.
    ASSERT_LE((int)ZL_GGUFType_limit, (int)GGML_TYPE_COUNT);
    for (uint32_t type = 0; type < ZL_GGUFType_limit; ++type) {
        const auto ggmlType = (ggml_type)type;
        size_t blockElts    = 0;
        size_t blockBytes   = 0;
        const bool isKnown  = ZL_GGUF_typeLayout(type, &blockElts, &blockBytes);
        const bool isRemoved = ggml_blck_size(ggmlType) == 0;

        EXPECT_EQ(isKnown, !isRemoved) << "type " << type;
        if (isKnown && !isRemoved) {
            EXPECT_EQ(blockElts, (size_t)ggml_blck_size(ggmlType))
                    << "type " << type;
            EXPECT_EQ(blockBytes, ggml_type_size(ggmlType)) << "type " << type;
        }
    }
}

TEST(GGUFGgmlTest, LexesTensorsWhereGgmlReadsThem)
{
    // A tensor of each type, of 1 to 4 dimensions, or none for unquantized
    // types.
    std::vector<GGUFTensorSpec> tensors;
    for (uint32_t type = 0; type < ZL_GGUFType_limit; ++type) {
        size_t blockElts = 0, blockBytes = 0;
        if (!ZL_GGUF_typeLayout(type, &blockElts, &blockBytes)) {
            continue;
        }
        std::vector<uint64_t> ne = { 2 * blockElts, 3, 2, 2 };
        ne.resize(blockElts == 1 ? type % 5 : 1 + type % 4);
        size_t nbElts = 1;
        for (const auto dim : ne) {
            nbElts *= dim;
        }
        tensors.push_back(
                { "t" + std::to_string(type),
                  type,
                  std::move(ne),
                  pseudoRandomBytes(nbElts / blockElts * blockBytes, type) });
    }
    for (const uint32_t alignment : { 32u, 64u }) {
        for (const char version : { 2, 3 }) {
            auto file = buildGGUF(tensors, alignment, flatMetadata());
            file[4]   = version;

            const auto expected = ggmlTensors(file);

            ASSERT_EQ(expected.size(), tensors.size())
                    << "alignment " << alignment << ", version " << +version;
            EXPECT_EQ(lexedTensors(file), expected)
                    << "alignment " << alignment << ", version " << +version;
        }
    }
}

} // namespace
} // namespace openzl::custom_parsers
