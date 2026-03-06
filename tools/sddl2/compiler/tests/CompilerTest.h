// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <gtest/gtest.h>
#include <iomanip>
#include <sstream>

#include "tools/sddl2/compiler/Compiler.h"
#include "tools/sddl2/compiler/Exception.h"
#include "tools/sddl2/compiler/parser/AST.h"

namespace openzl::sddl2::tests {

namespace {
using ArgVec = std::vector<ASTPtr>;

// Helper class to check that the tokenization/grouper/parser steps are working
// as expected
class TestCompiler : public Compiler {
   public:
    explicit TestCompiler(Compiler::Options options, bool optimize)
            : Compiler{ std::move(options) }, optimize_(optimize)
    {
    }

    std::string compile(std::string_view source, std::string_view filename)
    {
        auto tree = compile_ast(source, filename);
        return codegen_.generate(tree);
    }

    ASTVec compile_ast(std::string_view source, std::string_view filename)
    {
        const Source src{ source, filename };
        const auto tokens = tokenizer_.tokenize(src);
        const auto groups = grouper_.group(tokens);
        auto tree         = parser_.parse(groups);
        semantic_analyzer_.analyze(tree);
        if (optimize_) {
            tree = optimizer_.optimize(tree);
        }
        return tree;
    }

   private:
    bool optimize_;
};

void expect_node_eq(const ASTNode& lhs, const ASTNode& rhs)
{
    // Compare by printing to string - this works for all AST node types
    std::ostringstream lhs_ss, rhs_ss;
    lhs.print(lhs_ss, 0);
    rhs.print(rhs_ss, 0);
    EXPECT_EQ(lhs_ss.str(), rhs_ss.str());
}
} // namespace

class CompilerTest : public testing::Test {
   protected:
    void SetUp() override
    {
        compiler_ = std::make_unique<TestCompiler>(
                Compiler::Options{}.with_log(logs_).with_verbosity(verbosity_),
                optimize());
    }

    virtual bool optimize() const
    {
        return true;
    }

    void expect_error(std::string_view source, std::string_view msg)
    {
        try {
            compiler_->compile(source, "[local_input]");
        } catch (const CompilerException& ex) {
            EXPECT_NE(std::string{ ex.what() }.find(msg), std::string::npos)
                    << std::quoted(ex.what()) << "\nShould contain:\n  "
                    << std::quoted(msg) << "\n"
                    << "Compiler debug logs:\n"
                    << logs_.str();
            return;
        }
        EXPECT_TRUE(false) << "Should have thrown a CompilerException!\n"
                           << "Compiler debug logs:\n"
                           << logs_.str();
    }

    void expect_success(std::string_view source)
    {
        EXPECT_NO_THROW(compiler_->compile(source, "[local_input]"))
                << "Compiler debug logs:\n"
                << logs_.str();
    }

    void expect_ast(std::string_view source, ASTVec expected)
    {
        const auto actual = compiler_->compile_ast(source, "[local_input]");

        EXPECT_EQ(actual.size(), expected.size());
        for (size_t i = 0; i < actual.size(); ++i) {
            auto& actual_node   = *actual.at(i);
            auto& expected_node = *expected.at(i);
            expect_node_eq(actual_node, expected_node);
        }
    }

    int verbosity_{ 1 };
    std::stringstream logs_;
    std::unique_ptr<TestCompiler> compiler_;
};
} // namespace openzl::sddl2::tests
