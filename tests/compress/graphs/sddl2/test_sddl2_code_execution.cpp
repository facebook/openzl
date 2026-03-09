// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <gtest/gtest.h>

#include "tests/compress/graphs/sddl2/utils.h"

#include "openzl/compress/graphs/sddl2/sddl2_interpreter.h"
#include "tools/sddl2/assembler/Assembler.h"
#include "tools/sddl2/compiler/Compiler.h"

namespace openzl {
namespace sddl2 {
namespace testing {

class SDDL2CodeExecutionTest : public SDDL2TestBase {
   protected:
    void SetUp() override
    {
        SDDL2_Segment_list_init(&segments_, alloc_fn, alloc_ctx_);
    }

    void TearDown() override
    {
        SDDL2_Segment_list_destroy(&segments_);
    }

    template <typename T>
    void expect_success(
            const std::string& code,
            const std::vector<T>& input,
            const std::vector<size_t>& expected_segment_sizes)
    {
        expect_success(
                code,
                input.data(),
                input.size() * sizeof(T),
                expected_segment_sizes);
    }

   private:
    void expect_success(
            const std::string& code,
            const uint8_t* input,
            size_t input_size,
            const std::vector<size_t>& expected_segment_sizes)
    {
        EXPECT_EQ(run(code, input, input_size), SDDL2_OK);

        EXPECT_EQ(segments_.count, expected_segment_sizes.size());

        size_t offset = 0;
        for (size_t i = 0; i < expected_segment_sizes.size(); ++i) {
            EXPECT_EQ(segments_.items[i].tag, i + 1);
            EXPECT_EQ(segments_.items[i].start_pos, offset);
            EXPECT_EQ(segments_.items[i].size_bytes, expected_segment_sizes[i]);
            offset += expected_segment_sizes[i];
        }
    }

    SDDL2_Error run(const std::string& code, const uint8_t* input, size_t size)
    {
        auto assembly = compiler_.compile(code, "[test code]");
        auto bytecode = assembler_.assemble(std::move(assembly));
        return SDDL2_execute_bytecode(
                bytecode.data(), bytecode.size(), input, size, &segments_);
    }

    Assembler assembler_;
    Compiler compiler_;
    SDDL2_Segment_list segments_{};
};

// ============================================================================
// Consume Primitives
// ============================================================================

TEST_F(SDDL2CodeExecutionTest, ConsumeInts)
{
    const std::vector<size_t> expected_sizes = { 1, 1, 1, 2, 2, 4, 4, 8, 8 };
    const auto input = gen<uint8_t>(sum(expected_sizes));

    expect_success(
            R"(
                : Byte
                : Int8
                : UInt8
                : Int16LE
                : Int16BE
                : Int32LE
                : Int32BE
                : Int64LE
                : Int64BE
            )",
            input,
            expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, ConsumeFloats)
{
    const std::vector<size_t> expected_sizes = { 2, 2, 2, 2, 4, 4, 8, 8 };
    const auto input = gen<uint8_t>(sum(expected_sizes));

    expect_success(
            R"(
                : Float16LE
                : Float16BE
                : BFloat16LE
                : BFloat16BE
                : Float32LE
                : Float32BE
                : Float64LE
                : Float64BE
            )",
            input,
            expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, ConsumeBytes)
{
    const std::vector<size_t> expected_sizes = { 3, 4 };
    const auto input = gen<uint8_t>(sum(expected_sizes));

    expect_success(
            R"(
                : Bytes(3)
                : Byte[]
            )",
            input,
            expected_sizes);
}

// ============================================================================
// Consume Containers
// ============================================================================

TEST_F(SDDL2CodeExecutionTest, ConsumeRecord)
{
    const std::vector<size_t> expected_sizes = { 6 };
    const auto input = gen<uint8_t>(sum(expected_sizes));

    expect_success(
            R"(
                : Record() {id : Int16LE, val: Int32LE}
            )",
            input,
            expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, ConsumeArrayOfRecords)
{
    const std::vector<size_t> expected_sizes = { 2, 2 * 6, 3 * 6 };
    const auto input = gen<uint8_t>(sum(expected_sizes));

    expect_success(
            R"(
                : Record() {id : Int16LE}
                : Record() {id : Int16LE, val: Int32LE}[2]
                : Record() {id : Int16LE, val: Int32LE}[]
            )",
            input,
            expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, ConsumeArrayOfArrays)
{
    const std::vector<size_t> expected_sizes = { 2 * sizeof(int16_t),
                                                 2 * 2 * sizeof(int16_t),
                                                 2 * 4 * sizeof(int16_t) };
    const auto input = gen<uint8_t>(sum(expected_sizes));

    expect_success(
            R"(
                : Int16LE[2]
                : Int16LE[2][2]
                : Int16LE[2][]
            )",
            input,
            expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, ConsumeSimpleAnonymous)
{
    const size_t star_entry_size = 2 * sizeof(double) + 2 * sizeof(uint8_t)
            + sizeof(int16_t) + 2 * sizeof(float);
    const std::vector<size_t> expected_sizes = { 28, 4 * star_entry_size };
    const auto input = gen<uint8_t>(sum(expected_sizes));

    expect_success(
            R"(
                header : Bytes(28)
                stars : Record() {
                    SRA0 : Float64LE, # Right Ascension(radians)
                    SDEC0 : Float64LE, # Declination(radians)
                    ISP : Bytes(2), # Spectral type
                    MAG : Int16LE, # Magnitude
                    XRPM : Float32LE, # R.A. proper motion
                    XDPM : Float32LE # Dec. proper motion
                }[]
            )",
            input,
            expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, ConsumeSimpleSAO)
{
    const size_t star_entry_size = 2 * sizeof(double) + 2 * sizeof(uint8_t)
            + sizeof(int16_t) + 2 * sizeof(float);
    const std::vector<size_t> expected_sizes = { 28, 4 * star_entry_size };
    const auto input = gen<uint8_t>(sum(expected_sizes));

    expect_success(
            R"(
                # Star catalog entry (28 bytes)
                Record StarEntry() = {
                    SRA0:  Float64LE,    # Right Ascension (radians)
                    SDEC0: Float64LE,    # Declination (radians)
                    ISP:   Bytes(2),     # Spectral type
                    MAG:   Int16LE,      # Magnitude
                    XRPM:  Float32LE,    # R.A. proper motion
                    XDPM:  Float32LE     # Dec. proper motion
                }

                # File structure
                header: Bytes(28)
                stars: StarEntry[]
            )",
            input,
            expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, ConsumeArrayTypeVars)
{
    const std::vector<size_t> expected_sizes = { 16, 16 * 10, 12, 12 * 10 };
    const auto input = gen<uint8_t>(sum(expected_sizes));

    expect_success(
            R"(
                Foo = Int32LE[4]
                Bar = Int32LE[3]
                : Foo
                : Foo[10]
                : Bar
                : Bar[10]
            )",
            input,
            expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, VarRecordType)
{
    const size_t record_size = sizeof(int32_t) + sizeof(int16_t);
    const std::vector<size_t> expected_sizes = { record_size, 3 * record_size };
    const auto input = gen<uint8_t>(sum(expected_sizes));

    expect_success(
            R"(
                Entry = Record() { x: Int32LE, y: Int16LE }
                : Entry
                : Entry[3]
            )",
            input,
            expected_sizes);
}

} // namespace testing
} // namespace sddl2
} // namespace openzl
