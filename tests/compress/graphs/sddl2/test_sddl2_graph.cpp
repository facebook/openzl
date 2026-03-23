// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "tests/compress/graphs/sddl2/utils.h"

#include "tools/sddl2/assembler/Assembler.h"
#include "tools/sddl2/compiler/Compiler.h"

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/codecs/SDDL2.hpp"

namespace openzl {
namespace sddl2 {
namespace testing {

class SDDL2GraphTest : public SDDL2TestBase {
   protected:
    template <typename T>
    void expect_success(const std::string& code, const std::vector<T>& input)
    {
        poly::string_view input_view(
                (const char*)input.data(), input.size() * sizeof(T));

        auto compressed   = compress(code, input_view);
        auto decompressed = decompress(compressed);

        EXPECT_EQ(input_view, decompressed);
    }

    template <typename T>
    void expect_error(
            const std::string& code,
            const std::vector<T>& input,
            const std::string& msg)
    {
        poly::string_view input_view(
                (const char*)input.data(), input.size() * sizeof(T));
        try {
            compress(code, input_view);
        } catch (const openzl::Exception& ex) {
            EXPECT_NE(std::string{ ex.what() }.find(msg), std::string::npos);
            return;
        }
        EXPECT_TRUE(false) << "Should have thrown an openzl::Exception!"
                           << std::endl;
    }

   private:
    std::string compress(
            const std::string& code,
            const poly::string_view& input)
    {
        auto assembly = compiler_.compile(code, "[test code]");
        auto bytecode = assembler_.assemble(std::move(assembly));

        const poly::string_view bytecode_view(
                (const char*)bytecode.data(), bytecode.size());

        auto graphID = graphs::SDDL2(bytecode_view, ZL_GRAPH_STORE)
                               .parameterize(compressor_);
        compressor_.selectStartingGraph(graphID);

        cctx_.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        cctx_.refCompressor(compressor_);

        return cctx_.compressSerial(input);
    }

    std::string decompress(const std::string& input)
    {
        return dctx_.decompressSerial(input);
    }

    Assembler assembler_;
    Compiler compiler_;
    CCtx cctx_;
    DCtx dctx_;
    Compressor compressor_;
};

// ============================================================================
// Scalar / Atomic Types
// ============================================================================

TEST_F(SDDL2GraphTest, CompressFixedNumericArray)
{
    const auto input = gen<int64_t>(30);
    expect_success(
            R"(
                : Int64LE[10]
                : Float64LE[10]
                : Int64BE[2 * 4 + 2]
            )",
            input);
}

TEST_F(SDDL2GraphTest, CompressFixedBytesArray)
{
    const auto input = gen<uint8_t>(20);
    expect_success(
            R"(
                : Bytes(10)[2]
            )",
            input);
}

TEST_F(SDDL2GraphTest, ExpectArithmetic)
{
    const auto input = gen<uint8_t>(20);
    expect_success(
            R"(
                expect 5 + 10 == 15
                expect -5 + 10 == 5
                expect 5 - 10 == -5
                expect 10 - -5 == 15

                expect 5 * 10 == 50
                expect 5 / 10 == 0
                expect 5 % 10 == 5

                expect 5 & 5 == 5
                expect 5 | 5 == 5
                expect 5 ^ 5 == 0

                : Byte[]
            )",
            input);
}

// ============================================================================
// Complex Arrays
// ============================================================================

TEST_F(SDDL2GraphTest, CompressArrayOfArray)
{
    const auto input = gen<uint8_t>(10 * sizeof(int32_t) * 100);

    expect_success(
            R"(
                : Int32LE[10][]
            )",
            input);
}

TEST_F(SDDL2GraphTest, CompressSimpleSAO)
{
    const size_t star_entry_size = 2 * sizeof(double) + 2 * sizeof(uint8_t)
            + sizeof(int16_t) + 2 * sizeof(float);
    const auto input = gen<uint8_t>(28 + 100 * star_entry_size);

    expect_success(
            R"(
                header : Bytes(28)
                stars : Record() {
                    SRA0 : Float64LE, # Right Ascension (radians)
                    SDEC0 : Float64LE, # Declination (radians)
                    ISP : Bytes(2), # Spectral type
                    MAG : Int16LE, # Magnitude
                    XRPM : Float32LE, # R.A. proper motion
                    XDPM : Float32LE # Dec. proper motion
                }[]
            )",
            input);
}

// ============================================================================
// Error Scenarios
// ============================================================================

TEST_F(SDDL2GraphTest, CompressInputSizeNotMultipleOfRecordSize)
{
    const auto input = gen<uint8_t>(2 * sizeof(int32_t) * 100 + 1);

    expect_error(
            R"(
                : Record() {
                    a : Int32LE,
                    b : Int32LE,
                }[]
            )",
            input,
            "split instructions do not map exactly the entire input");
}

TEST_F(SDDL2GraphTest, ExpectFail)
{
    const auto input = gen<uint8_t>(10);

    expect_error(
            R"(
                expect 5 + 5 == 15

                : Byte[]
            )",
            input,
            "expect_true condition not met");
}

} // namespace testing
} // namespace sddl2
} // namespace openzl
