// Copyright (c) Meta Platforms, Inc. and affiliates.
#include "custom_parsers/parquet/parquet_lexer.h"
#include "custom_parsers/parquet/tests/fuzz_utils.h"
#include "custom_parsers/parquet/tests/test_utils.h"
#include "openzl/common/errors_internal.h"

namespace zstrong {
namespace parquet {
namespace testing {
namespace {

class ParquetLexerTest : public ::testing::Test {
   public:
    ParquetLexerTest()
    {
        lexer_ = ZL_ParquetLexer_create();
    }
    ~ParquetLexerTest()
    {
        ZL_ParquetLexer_free(lexer_);
    }

    void reset()
    {
        ZL_ParquetLexer_free(lexer_);
        lexer_ = ZL_ParquetLexer_create();
    }

    ZL_ParquetLexer* lexer_;
};
} // namespace

FUZZ_F(ParquetLexerTest, FuzzLexerRandomInput)
{
    std::string_view data{ (const char*)Data, Size };
    std::array<ZL_ParquetToken, 10> tokens{};

    auto report =
            ZL_ParquetLexer_init(lexer_, data.data(), data.size(), nullptr);
    if (ZL_isError(report)) {
        return;
    }

    size_t offset = 0;
    while (!ZL_ParquetLexer_finished(lexer_)) {
        report = ZL_ParquetLexer_lex(
                lexer_, tokens.data(), tokens.size(), nullptr);
        if (ZL_isError(report)) {
            return;
        }
        const auto numTokens = ZL_validResult(report);
        ZL_REQUIRE_LE(numTokens, tokens.size());
        for (size_t i = 0; i < numTokens; ++i) {
            ZL_REQUIRE_EQ(tokens.at(i).ptr, data.data() + offset);
            offset += tokens.at(i).size;
        }
    }
    ZL_REQUIRE_EQ(offset, data.size());
}

FUZZ_F(ParquetLexerTest, FuzzLexerValidInput)
{
    auto schema = gen_arrow_schema(f, 2 /* max depth */);
    auto data   = gen_parquet_from_schema(f, schema);

    std::array<ZL_ParquetToken, 10> tokens{};

    auto report =
            ZL_ParquetLexer_init(lexer_, data.data(), data.size(), nullptr);
    ZL_REQUIRE_SUCCESS(report);

    size_t offset = 0;
    while (!ZL_ParquetLexer_finished(lexer_)) {
        report = ZL_ParquetLexer_lex(
                lexer_, tokens.data(), tokens.size(), nullptr);
        ZL_REQUIRE_SUCCESS(report);
        const auto numTokens = ZL_validResult(report);
        ZL_REQUIRE_LE(numTokens, tokens.size());
        for (size_t i = 0; i < numTokens; ++i) {
            ZL_REQUIRE_EQ(tokens.at(i).ptr, data.data() + offset);
            offset += tokens.at(i).size;
        }
    }
    ZL_REQUIRE_EQ(offset, data.size());
}
} // namespace testing
} // namespace parquet
} // namespace zstrong
