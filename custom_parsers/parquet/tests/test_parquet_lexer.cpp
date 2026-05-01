// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <arrow/api.h>    // @manual
#include <arrow/io/api.h> // @manual
#include <parquet/arrow/writer.h>
#include <parquet/exception.h>
#include <stdint.h>

#include <gtest/gtest.h>

#include "custom_parsers/parquet/parquet_lexer.h"
#include "custom_parsers/parquet/tests/test_utils.h"
#include "openzl/common/errors_internal.h"
#include "openzl/shared/mem.h"
#include "openzl/shared/xxhash.h"

namespace zstrong {
namespace parquet {
namespace testing {

namespace {
std::shared_ptr<arrow::Table> generate_table(bool nullable = true)
{
    auto i64array = to_arrow_array<int64_t>({ 100, 200, 300, 400, 500 });
    auto strarray = to_arrow_array<std::string>(
            { "hello", "world", "my", "name", "is" });

    std::shared_ptr<arrow::Schema> schema = arrow::schema(
            { arrow::field("int", arrow::int64(), nullable),
              arrow::field("str", arrow::utf8(), nullable) });

    return arrow::Table::Make(schema, { i64array, strarray });
}

std::shared_ptr<arrow::Table> generate_nested_table()
{
    // Top-Level Int Column
    auto i64array = to_arrow_array<int64_t>({ 100, 200, 300, 400, 500 });

    // Nested Struct Column
    auto i128 = arrow::fixed_size_binary(16);
    auto level2_t =
            arrow::struct_({ { "int", arrow::int32() }, { "struct", i128 } });
    auto level1_t =
            arrow::struct_({ { "str", arrow::utf8() }, { "2", level2_t } });
    std::shared_ptr<arrow::Schema> schema = arrow::schema(
            { arrow::field("int", arrow::int64()),
              arrow::field("1", level1_t) });

    // Fill in level 2
    auto i32array = to_arrow_array<int32_t>({ 1, 2, 3, 4, 5 });
    std::string i128data(16, 'a');
    auto i128array = to_arrow_array(
            { i128data, i128data, i128data, i128data, i128data }, 16);

    auto level2 = std::make_shared<arrow::StructArray>(arrow::StructArray(
            level2_t, 5, { i32array, i128array }, nullptr, 0, 0));

    // Fill in level 1
    auto strarray = to_arrow_array<std::string>(
            { "hello", "world", "my", "name", "is" });
    auto level1 = std::make_shared<arrow::StructArray>(arrow::StructArray(
            level1_t, 5, { strarray, level2 }, nullptr, 0, 0));

    // Create table from top-level columns
    return arrow::Table::Make(schema, { i64array, level1 });
}

struct Column {
    std::vector<std::string> path;
    ZL_Type dataType;
    size_t dataWidth;
};

uint32_t getTag(const std::vector<std::string>& path)
{
    XXH3_state_t state;
    XXH3_64bits_reset(&state);
    for (const auto& str : path) {
        size_t len = str.size();
        XXH3_64bits_update(&state, str.data(), len);
        XXH3_64bits_update(&state, &len, sizeof(size_t));
    }
    XXH64_hash_t result = XXH3_64bits_digest(&state);
    return (uint32_t)result;
}

void validateTokens(
        const std::vector<ZL_ParquetToken>& tokens,
        const std::string& input,
        const std::vector<Column>& columns,
        size_t num_row_groups)
{
    int i = 0;
    // Magic
    EXPECT_EQ(tokens.at(i).type, ZL_ParquetTokenType_Magic);

    // Data Headers + Pages
    for (size_t rg = 0; rg < num_row_groups; rg++) {
        for (size_t j = 0; j < columns.size(); j++) {
            i++;
            EXPECT_EQ(tokens.at(i).type, ZL_ParquetTokenType_PageHeader);
            i++;
            auto& token    = tokens.at(i);
            auto& expected = columns.at(j);
            EXPECT_EQ(token.type, ZL_ParquetTokenType_DataPage);
            EXPECT_EQ(token.tag, getTag(expected.path));
            EXPECT_EQ(token.dataType, expected.dataType);
            EXPECT_EQ(token.dataWidth, expected.dataWidth);
            EXPECT_EQ(token.size % token.dataWidth, 0);
        }
    }
    // Footer
    i++;
    EXPECT_EQ(tokens.at(i).type, ZL_ParquetTokenType_Footer);

    // Token sizes should add up to the input size
    auto sum = 0;
    for (auto const& token : tokens) {
        sum += token.size;
        EXPECT_NE(token.ptr, nullptr);
    }
    EXPECT_EQ(sum, input.size());
}
} // namespace

TEST(ParquetLexerTest, TestInitValidParquet)
{
    auto lexer = ZL_ParquetLexer_create();
    EXPECT_NE(lexer, nullptr);

    auto input = to_canonical_parquet(generate_table(), 3);

    ZL_REQUIRE_SUCCESS(
            ZL_ParquetLexer_init(lexer, input.data(), input.size(), nullptr));

    ZL_ParquetLexer_free(lexer);
}

TEST(ParquetLexerTest, TestInitNonParquet)
{
    auto lexer = ZL_ParquetLexer_create();
    EXPECT_NE(lexer, nullptr);
    std::string input = "hello world";

    EXPECT_TRUE(ZL_isError(
            ZL_ParquetLexer_init(lexer, input.data(), input.size(), nullptr)));

    ZL_ParquetLexer_free(lexer);
}

TEST(ParquetLexerTest, TestInitInvalidMetadataSize)
{
    auto lexer = ZL_ParquetLexer_create();
    EXPECT_NE(lexer, nullptr);

    auto input = to_canonical_parquet(generate_table(), 3);

    ZL_writeLE32(
            input.data() + (input.size() - 8),
            std::numeric_limits<uint32_t>::max());

    EXPECT_TRUE(ZL_isError(
            ZL_ParquetLexer_init(lexer, input.data(), input.size(), nullptr)));

    ZL_ParquetLexer_free(lexer);
}

TEST(ParquetLexerTest, TestLexValidParquet)
{
    auto lexer = ZL_ParquetLexer_create();
    EXPECT_NE(lexer, nullptr);

    auto input = to_canonical_parquet(generate_table(), 3);

    ZL_REQUIRE_SUCCESS(
            ZL_ParquetLexer_init(lexer, input.data(), input.size(), nullptr));

    auto tokens = std::vector<ZL_ParquetToken>(15);

    auto const res =
            ZL_ParquetLexer_lex(lexer, tokens.data(), tokens.size(), nullptr);
    EXPECT_FALSE(ZL_isError(res));
    EXPECT_TRUE(ZL_ParquetLexer_finished(lexer));
    auto const numTokens = ZL_validResult(res);
    EXPECT_LT(numTokens, tokens.size());
    tokens.resize(numTokens);

    std::vector<Column> columns = { { { "int" }, ZL_Type_numeric, 8 },
                                    { { "str" }, ZL_Type_serial, 1 } };

    validateTokens(tokens, input, columns, 2);

    ZL_ParquetLexer_free(lexer);
}

TEST(ParquetLexerTest, TestLexRequiredParquet)
{
    auto lexer = ZL_ParquetLexer_create();
    EXPECT_NE(lexer, nullptr);

    auto input = to_canonical_parquet(generate_table(false /* nullable */), 3);

    ZL_REQUIRE_SUCCESS(
            ZL_ParquetLexer_init(lexer, input.data(), input.size(), nullptr));

    auto tokens = std::vector<ZL_ParquetToken>(15);

    auto const res =
            ZL_ParquetLexer_lex(lexer, tokens.data(), tokens.size(), nullptr);
    EXPECT_FALSE(ZL_isError(res));
    EXPECT_TRUE(ZL_ParquetLexer_finished(lexer));
    auto const numTokens = ZL_validResult(res);
    EXPECT_LT(numTokens, tokens.size());
    tokens.resize(numTokens);

    std::vector<Column> columns = { { { "int" }, ZL_Type_numeric, 8 },
                                    { { "str" }, ZL_Type_serial, 1 } };

    validateTokens(tokens, input, columns, 2);

    ZL_ParquetLexer_free(lexer);
}

TEST(ParquetLexerTest, TestLexNestedParquet)
{
    auto lexer = ZL_ParquetLexer_create();
    EXPECT_NE(lexer, nullptr);

    auto input = to_canonical_parquet(generate_nested_table(), 3);

    ZL_REQUIRE_SUCCESS(
            ZL_ParquetLexer_init(lexer, input.data(), input.size(), nullptr));

    auto tokens = std::vector<ZL_ParquetToken>(20);

    auto const res =
            ZL_ParquetLexer_lex(lexer, tokens.data(), tokens.size(), nullptr);
    EXPECT_FALSE(ZL_isError(res));
    EXPECT_TRUE(ZL_ParquetLexer_finished(lexer));
    auto const numTokens = ZL_validResult(res);
    EXPECT_LT(numTokens, tokens.size());
    tokens.resize(numTokens);

    std::vector<Column> columns = {
        { { "int" }, ZL_Type_numeric, 8 },
        { { "1", "str" }, ZL_Type_serial, 1 },
        { { "1", "2", "int" }, ZL_Type_numeric, 4 },
        { { "1", "2", "struct" }, ZL_Type_struct, 16 }
    };

    validateTokens(tokens, input, columns, 2);

    ZL_ParquetLexer_free(lexer);
}
} // namespace testing
} // namespace parquet
} // namespace zstrong
