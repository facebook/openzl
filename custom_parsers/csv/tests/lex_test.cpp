// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "custom_parsers/csv/csv_lexer.h"
#include "openzl/zl_errors.h"
#include "tests/utils.h"

using namespace ::testing;

class LexTest : public ::testing::Test {
   public:
    std::vector<ZL_CSV_Token>
    lex(std::string_view csv, char sep = ',', bool isNullAware = false)
    {
        std::vector<ZL_CSV_Token> tokens;
        ZL_CSV_Lexer lexer;
        ZL_CSV_Lexer_init(
                &lexer, NULL, csv.data(), csv.size(), sep, isNullAware);
        while (!ZL_CSV_Lexer_finished(&lexer)) {
            ZL_CSV_Token tok;
            auto report = ZL_CSV_Lexer_lex(
                    &lexer, &tok.type, &tok.size, &tok.col, 1, 0);
            ZL_REQUIRE_SUCCESS(report);
            EXPECT_EQ(ZL_validResult(report), 1);
            tokens.push_back(tok);
        }

        // Validate that lexing in one go produces exactly the same result
        {
            std::vector<ZL_CSV_TokenType> types(tokens.size());
            std::vector<uint32_t> sizes(tokens.size());
            std::vector<uint32_t> cols(tokens.size());
            ZL_CSV_Lexer_init(
                    &lexer, NULL, csv.data(), csv.size(), sep, isNullAware);
            auto report = ZL_CSV_Lexer_lex(
                    &lexer,
                    types.data(),
                    sizes.data(),
                    cols.data(),
                    tokens.size(),
                    size_t(-1));
            ZL_REQUIRE_SUCCESS(report);
            EXPECT_EQ(ZL_validResult(report), tokens.size());
            for (size_t i = 0; i < tokens.size(); ++i) {
                EXPECT_EQ(tokens[i].type, types[i]);
                EXPECT_EQ(tokens[i].size, sizes[i]);
                EXPECT_EQ(tokens[i].col, cols[i]);
            }
        }

        return tokens;
    }

    bool
    lexSucceeds(std::string_view csv, char sep = ',', bool isNullAware = false)
    {
        ZL_CSV_Lexer lexer;
        ZL_CSV_Lexer_init(
                &lexer, NULL, csv.data(), csv.size(), sep, isNullAware);
        while (!ZL_CSV_Lexer_finished(&lexer)) {
            ZL_CSV_Token tok;
            auto report = ZL_CSV_Lexer_lex(
                    &lexer, &tok.type, &tok.size, &tok.col, 1, 0);
            if (ZL_isError(report)) {
                return false;
            }
            EXPECT_EQ(ZL_validResult(report), 1);
        }
        return true;
    }
};

TEST_F(LexTest, Empty)
{
    auto tokens = lex("");
    ASSERT_EQ(tokens.size(), 0);
}

TEST_F(LexTest, SingleRowAndCol)
{
    auto tokens = lex("a");
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 1);
    EXPECT_EQ(tokens[0].col, 0);
}

TEST_F(LexTest, SingleRowAndColWithNewline)
{
    auto tokens = lex("a\n");
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 1);
    EXPECT_EQ(tokens[0].col, 0);
    EXPECT_EQ(tokens[1].type, ZL_CSV_TokenType_Newline);
    EXPECT_EQ(tokens[1].size, 1);
    EXPECT_EQ(tokens[1].col, 0);

    tokens = lex("a");
    ASSERT_EQ(tokens.size(), 1);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 1);
    EXPECT_EQ(tokens[0].col, 0);
}

TEST_F(LexTest, SingleColumn)
{
    auto tokens = lex("a\nbb\n");
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 1);
    EXPECT_EQ(tokens[0].col, 0);
    EXPECT_EQ(tokens[1].type, ZL_CSV_TokenType_Newline);
    EXPECT_EQ(tokens[1].size, 1);
    EXPECT_EQ(tokens[1].col, 0);
    EXPECT_EQ(tokens[2].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[2].size, 2);
    EXPECT_EQ(tokens[2].col, 0);
    EXPECT_EQ(tokens[3].type, ZL_CSV_TokenType_Newline);
    EXPECT_EQ(tokens[3].size, 1);
    EXPECT_EQ(tokens[3].col, 0);
}

TEST_F(LexTest, UnterminatedQuotedString)
{
    EXPECT_FALSE(lexSucceeds("\"a\nbb\n"));
}

TEST_F(LexTest, QuotedString)
{
    auto tokens = lex("\"field,with,commas\",\"field\nwith\nnewline\"\n");
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 19);
    EXPECT_EQ(tokens[0].col, 0);
    EXPECT_EQ(tokens[1].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[1].size, 1);
    EXPECT_EQ(tokens[1].col, 0);
    EXPECT_EQ(tokens[2].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[2].size, 20);
    EXPECT_EQ(tokens[2].col, 1);
    EXPECT_EQ(tokens[3].type, ZL_CSV_TokenType_Newline);
    EXPECT_EQ(tokens[3].size, 1);
    EXPECT_EQ(tokens[3].col, 0);
}

TEST_F(LexTest, ConsecutiveSeparatorsNullAware)
{
    auto tokens = lex("a,,,,b\n", ',', true);
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 1);
    EXPECT_EQ(tokens[0].col, 0);
    EXPECT_EQ(tokens[1].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[1].size, 4);
    EXPECT_EQ(tokens[1].col, 0);
    EXPECT_EQ(tokens[2].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[2].size, 1);
    EXPECT_EQ(tokens[2].col, 4);
    EXPECT_EQ(tokens[3].type, ZL_CSV_TokenType_Newline);
    EXPECT_EQ(tokens[3].size, 1);
    EXPECT_EQ(tokens[3].col, 0);
}

TEST_F(LexTest, InconsistentColumnCounts)
{
    EXPECT_FALSE(lexSucceeds("a,b,c\nd,e\n"));
    EXPECT_FALSE(lexSucceeds("a\nb,c\n"));
}

TEST_F(LexTest, EmptyFieldsAtVariousPositions)
{
    auto tokens = lex(",b,c\n");
    ASSERT_EQ(tokens.size(), 6);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 0);
    EXPECT_EQ(tokens[0].col, 0);
    EXPECT_EQ(tokens[1].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[2].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[2].size, 1);
    EXPECT_EQ(tokens[2].col, 1);
    EXPECT_EQ(tokens[3].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[4].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[4].size, 1);
    EXPECT_EQ(tokens[4].col, 2);
    EXPECT_EQ(tokens[5].type, ZL_CSV_TokenType_Newline);

    tokens = lex("a,,c\n");
    ASSERT_EQ(tokens.size(), 6);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 1);
    EXPECT_EQ(tokens[0].col, 0);
    EXPECT_EQ(tokens[1].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[2].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[2].size, 0);
    EXPECT_EQ(tokens[2].col, 1);
    EXPECT_EQ(tokens[3].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[4].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[4].size, 1);
    EXPECT_EQ(tokens[4].col, 2);
    EXPECT_EQ(tokens[5].type, ZL_CSV_TokenType_Newline);

    tokens = lex("a,b,\n");
    ASSERT_EQ(tokens.size(), 6);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 1);
    EXPECT_EQ(tokens[0].col, 0);
    EXPECT_EQ(tokens[1].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[2].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[2].size, 1);
    EXPECT_EQ(tokens[2].col, 1);
    EXPECT_EQ(tokens[3].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[4].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[4].size, 0);
    EXPECT_EQ(tokens[4].col, 2);
    EXPECT_EQ(tokens[5].type, ZL_CSV_TokenType_Newline);
}

TEST_F(LexTest, AlternativeSeparators)
{
    auto tokens = lex("a,\t|\"|\"\n", '|');
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 3);
    EXPECT_EQ(tokens[0].col, 0);
    EXPECT_EQ(tokens[1].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[1].size, 1);
    EXPECT_EQ(tokens[1].col, 0);
    EXPECT_EQ(tokens[2].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[2].size, 3);
    EXPECT_EQ(tokens[2].col, 1);
    EXPECT_EQ(tokens[3].type, ZL_CSV_TokenType_Newline);
    EXPECT_EQ(tokens[3].size, 1);
    EXPECT_EQ(tokens[3].col, 0);

    tokens = lex("a,|\t\"\t\"\n", '\t');
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 3);
    EXPECT_EQ(tokens[0].col, 0);
    EXPECT_EQ(tokens[1].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[1].size, 1);
    EXPECT_EQ(tokens[1].col, 0);
    EXPECT_EQ(tokens[2].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[2].size, 3);
    EXPECT_EQ(tokens[2].col, 1);
    EXPECT_EQ(tokens[3].type, ZL_CSV_TokenType_Newline);
    EXPECT_EQ(tokens[3].size, 1);
    EXPECT_EQ(tokens[3].col, 0);
}

TEST_F(LexTest, SrcSizeLimitBoundaryConditions)
{
    std::string_view csv = "a,b\nc,d\ne,f\n";

    ZL_CSV_Lexer lexer;
    ZL_CSV_Lexer_init(&lexer, NULL, csv.data(), csv.size(), ',', false);

    ZL_CSV_TokenType types[10];
    uint32_t sizes[10];
    uint32_t cols[10];

    auto report = ZL_CSV_Lexer_lex(&lexer, types, sizes, cols, 10, 3);
    ZL_REQUIRE_SUCCESS(report);
    EXPECT_EQ(ZL_validResult(report), 4);
    EXPECT_EQ(types[0], ZL_CSV_TokenType_Field);
    EXPECT_EQ(types[1], ZL_CSV_TokenType_Sep);
    EXPECT_EQ(types[2], ZL_CSV_TokenType_Field);
    EXPECT_EQ(types[3], ZL_CSV_TokenType_Newline);

    EXPECT_FALSE(ZL_CSV_Lexer_finished(&lexer));

    report = ZL_CSV_Lexer_lex(&lexer, types, sizes, cols, 10, SIZE_MAX);
    ZL_REQUIRE_SUCCESS(report);
    EXPECT_GT(ZL_validResult(report), 0);
    EXPECT_TRUE(ZL_CSV_Lexer_finished(&lexer));
}

TEST_F(LexTest, BatchedLexingConsistency)
{
    auto tokens = lex("a,b,c\nd,e,f\n");
    ASSERT_EQ(tokens.size(), 12);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[1].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[2].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[3].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[4].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[5].type, ZL_CSV_TokenType_Newline);
    EXPECT_EQ(tokens[6].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[7].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[8].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[9].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[10].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[11].type, ZL_CSV_TokenType_Newline);
}

TEST_F(LexTest, FilesWithoutTrailingNewline)
{
    auto tokensWithNewline    = lex("a,b,c\n");
    auto tokensWithoutNewline = lex("a,b,c");

    ASSERT_EQ(tokensWithNewline.size(), 6);
    ASSERT_EQ(tokensWithoutNewline.size(), 5);

    for (size_t i = 0; i < tokensWithoutNewline.size(); ++i) {
        EXPECT_EQ(tokensWithoutNewline[i].type, tokensWithNewline[i].type);
        EXPECT_EQ(tokensWithoutNewline[i].size, tokensWithNewline[i].size);
        EXPECT_EQ(tokensWithoutNewline[i].col, tokensWithNewline[i].col);
    }
}

TEST_F(LexTest, EmptyRowsAndSingleColumnEdgeCases)
{
    auto tokens = lex("\n");
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 0);
    EXPECT_EQ(tokens[1].type, ZL_CSV_TokenType_Newline);

    tokens = lex(",\n");
    ASSERT_EQ(tokens.size(), 4);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 0);
    EXPECT_EQ(tokens[1].type, ZL_CSV_TokenType_Sep);
    EXPECT_EQ(tokens[2].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[2].size, 0);
    EXPECT_EQ(tokens[3].type, ZL_CSV_TokenType_Newline);
}

TEST_F(LexTest, EscapedQuotesWithinQuotedFields)
{
    auto tokens = lex("\"He said \"\"Hello\"\" to me\"\n");
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 25);
    EXPECT_EQ(tokens[0].col, 0);
    EXPECT_EQ(tokens[1].type, ZL_CSV_TokenType_Newline);
    EXPECT_EQ(tokens[1].size, 1);
    EXPECT_EQ(tokens[1].col, 0);
}

TEST_F(LexTest, EscapedQuotesInMiddleOfUnquotedField)
{
    auto tokens = lex("x\"y\"z\"a\"\"b\"\"c\"d\n");
    ASSERT_EQ(tokens.size(), 2);
    EXPECT_EQ(tokens[0].type, ZL_CSV_TokenType_Field);
    EXPECT_EQ(tokens[0].size, 15);
    EXPECT_EQ(tokens[0].col, 0);
    EXPECT_EQ(tokens[1].type, ZL_CSV_TokenType_Newline);
    EXPECT_EQ(tokens[1].size, 1);
    EXPECT_EQ(tokens[1].col, 0);
}
