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
                : record() {id : Int16LE, val: Int32LE}
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
                : record() {id : Int16LE}
                : record() {id : Int16LE, val: Int32LE}[2]
                : record() {id : Int16LE, val: Int32LE}[]
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
                stars : record() {
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
                record StarEntry() {
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
                Entry = record() { x: Int32LE, y: Int16LE }
                : Entry
                : Entry[3]
            )",
            input,
            expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, AssumeBuiltInType)
{
    const std::vector<size_t> expected_sizes = { 4, 2 };
    std::vector<uint8_t> input               = {
        0x01, 0x00, 0x00, 0x00, 0x02, 0x00,
    };

    const auto prog = R"(
        x : Int32LE
        y : Int16LE
        expect x == 1
        expect y == 2
    )";

    expect_success(prog, input, expected_sizes);
}
TEST_F(SDDL2CodeExecutionTest, AssumeRecord)
{
    const size_t record_size = sizeof(int32_t) + sizeof(int16_t);
    const std::vector<size_t> expected_sizes = { record_size };
    std::vector<uint8_t> input               = {
        0x01, 0x00, 0x00, 0x00, 0x02, 0x00,
    };

    const auto prog = R"(
        record Foo() {
            x: Int32LE,
            y: Int16LE
        }
        foo: Foo
        expect foo.x == 1
        expect foo.y == 2

    )";

    expect_success(prog, input, expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, AssumeNestedRecord)
{
    const std::vector<size_t> expected_sizes = {
        sizeof(int32_t) + sizeof(int16_t) + sizeof(char)
    };
    std::vector<uint8_t> input = {
        0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03,
    };

    const auto prog = R"(
        record Bar() {
            x : Int32LE,
            y : Int16LE,
        }
        record Foo() {
            bar : Bar,
            x : Byte,
        }

        foo : Foo
        expect foo.bar.x == 1
        expect foo.bar.y == 2
        expect foo.x == 3
    )";
    expect_success(prog, input, expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, AssumeAnonymousNestedRecord)
{
    const std::vector<size_t> expected_sizes = {
        sizeof(int32_t) + sizeof(int16_t) + sizeof(char)
    };
    std::vector<uint8_t> input = {
        0x01, 0x00, 0x00, 0x00, 0x02, 0x00, 0x03,
    };

    const auto prog = R"(
        record Foo() {
            bar : record() { x : Int32LE, y : Int16LE },
            x : Byte,
        }

        foo : Foo
        expect foo.bar.x == 1
        expect foo.bar.y == 2
        expect foo.x == 3
    )";
    expect_success(prog, input, expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, AssumeRecordWithGlobalVariableReference)
{
    const std::vector<size_t> expected_sizes = {
        1,
        4,
    };
    std::vector<uint8_t> input = {
        /* len */ 0x03,
        /* foo */ 0x01, 0x02, 0x03, 0x01,
    };

    const auto prog = R"(
        len: Byte
        record Foo() {
            a : Bytes(len),
            b : Byte,
        }
        foo : Foo
        expect len == 3
        expect foo.b == 1
    )";
    expect_success(prog, input, expected_sizes);
}

// ============================================================================
// Parameterized Records
// ============================================================================

TEST_F(SDDL2CodeExecutionTest, ConsumeParameterizedRecord)
{
    const std::vector<size_t> expected_sizes = { 40 };
    const auto input = gen<uint8_t>(sum(expected_sizes));

    const auto prog = R"(
        record Entry(N) {
            items: Int32LE[N]
        }
        : Entry(10)
    )";

    expect_success(prog, input, expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, ConsumeParameterizedRecordMultipleParams)
{
    const std::vector<size_t> expected_sizes = { 22 };
    const auto input = gen<uint8_t>(sum(expected_sizes));

    const auto prog = R"(
        record Foo(A, B) {
            x: Int32LE[A],
            y: Int16LE[B]
        }
        ParamFoo = Foo(3, 5)
        : ParamFoo
    )";

    expect_success(prog, input, expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, ConsumeParameterizedRecordMultipleCalls)
{
    const std::vector<size_t> expected_sizes = { 8, 12 };
    const auto input = gen<uint8_t>(sum(expected_sizes));

    const auto prog = R"(
        record Entry(N) {
            items: Int32LE[N]
        }
        : Entry(2)
        : Entry(3)
    )";

    expect_success(prog, input, expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, ConsumeArrayOfParameterizedRecord)
{
    const std::vector<size_t> expected_sizes = { 60 };
    const auto input = gen<uint8_t>(sum(expected_sizes));

    const auto prog = R"(
        record Entry(N) {
            items: Int32LE[N]
        }
        : Entry(5)[3]
    )";

    expect_success(prog, input, expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, AssumeParameterizedRecord)
{
    const std::vector<size_t> expected_sizes = { 1, 21 };
    std::vector<uint8_t> input(22, 0);
    input[0]  = 5; // n = 5
    input[21] = 3; // tag = 3

    const auto prog = R"(
        record Entry(N) {
            items: Int32LE[N],
            tag: Byte
        }
        n: Byte
        entry: Entry(n)
        expect entry.tag == 3
    )";

    expect_success(prog, input, expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, AssumeNestedParameterizedRecord)
{
    const std::vector<size_t> expected_sizes = { 1, 10 };
    std::vector<uint8_t> input(11, 0);
    input[0]  = 2; // n = 2
    input[9]  = 3; // foo.bar.tag = 3
    input[10] = 5; // foo.tag = 5

    const auto prog = R"(
        record Bar(N) {
            items: Int32LE[N],
            tag: Byte
        }
        record Foo(M) {
            bar: Bar(M),
            tag: Byte
        }
        N: Byte
        foo: Foo(N)
        expect foo.bar.tag == 3
        expect foo.tag == 5
        expect N == 2
    )";

    expect_success(prog, input, expected_sizes);
}

// ============================================================================
// When Blocks
// ============================================================================

TEST_F(SDDL2CodeExecutionTest, WhenBlockTrueCondition)
{
    const std::vector<size_t> expected_sizes = { 1, 4 };
    std::vector<uint8_t> input               = {
        0x01,                   // flag = 1 (true)
        0x0A, 0x00, 0x00, 0x00, // optional = 10
    };

    const auto prog = R"(
        flag: UInt8
        when flag {
            optional: Int32LE
        }
        expect flag == 1
    )";

    expect_success(prog, input, expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, WhenBlockFalseCondition)
{
    const std::vector<size_t> expected_sizes = { 1, 0 };
    std::vector<uint8_t> input               = {
        0x00, // flag = 0 (false)
    };

    const auto prog = R"(
        flag: UInt8
        when flag {
            optional: Int32LE
        }
        expect flag == 0
    )";

    expect_success(prog, input, expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, NestedWhenBlocks)
{
    const std::vector<size_t> expected_sizes = { 1, 1, 4 };
    std::vector<uint8_t> input               = {
        0x01,                   // a = 1
        0x02,                   // b = 2
        0x0A, 0x00, 0x00, 0x00, // optional = 10
    };

    const auto prog = R"(
        a: UInt8
        b: UInt8
        when a == 1 {
            when b == 2 {
                optional: Int32LE
            }
        }
        expect a == 1
        expect b == 2
    )";

    expect_success(prog, input, expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, NestedWhenBlocksOuterFalse)
{
    const std::vector<size_t> expected_sizes = { 1, 1, 0 };
    std::vector<uint8_t> input               = {
        0x00, // a = 0 (outer false)
        0x02, // b = 2
    };

    const auto prog = R"(
        a: UInt8
        b: UInt8
        when a == 1 {
            when b == 2 {
                optional: Int32LE
            }
        }
        expect a == 0
        expect b == 2
    )";

    expect_success(prog, input, expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, NestedWhenBlocksInnerFalse)
{
    const std::vector<size_t> expected_sizes = { 1, 1, 0 };
    std::vector<uint8_t> input               = {
        0x01, // a = 1 (outer true)
        0x05, // b = 5 (inner false, not 2)
    };

    const auto prog = R"(
        a: UInt8
        b: UInt8
        when a == 1 {
            when b == 2 {
                optional: Int32LE
            }
        }
        expect a == 1
        expect b == 5
    )";

    expect_success(prog, input, expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, WhenBlockInParameterizedRecord)
{
    const std::vector<size_t> expected_sizes = { 10 };
    std::vector<uint8_t> input               = {
        0x01, 0x00, 0x00, 0x00, // a = 1
        0x02, 0x00,             // optional = 2
        0x03, 0x00, 0x00, 0x00, // b = 3
    };

    const auto prog = R"(
        record Data(flag) {
            a: Int32LE,
            when flag {
                optional: Int16LE
            },
            b: Int32LE
        }
        data: Data(1)
        expect data.a == 1
        expect data.b == 3
    )";
    expect_success(prog, input, expected_sizes);
}

TEST_F(SDDL2CodeExecutionTest, WhenBlockInParameterizedRecordFalse)
{
    const std::vector<size_t> expected_sizes = { 8 };
    std::vector<uint8_t> input               = {
        0x01, 0x00, 0x00, 0x00, // a = 1
        0x02, 0x00, 0x00, 0x00, // b = 2
    };

    const auto prog = R"(
        record Data(flag) {
            a: Int32LE,
            when flag {
                optional: Int16LE
            },
            b: Int32LE
        }
        data: Data(0)
        expect data.a == 1
        expect data.b == 2
    )";

    expect_success(prog, input, expected_sizes);
}

// ============================================================================
// Runtime Value Assignment
// ============================================================================

TEST_F(SDDL2CodeExecutionTest, AssignRuntimeValue)
{
    const std::vector<size_t> expected_sizes = { 4, 4 };
    std::vector<uint8_t> input               = {
        0x05, 0x00, 0x00, 0x00, // x = 5
        0x03, 0x00, 0x00, 0x00, // y = 3
    };

    const auto prog = R"(
        x: Int32LE
        y: Int32LE
        sum = x + y
        expect x == 5
        expect y == 3
        expect sum == 8
    )";

    expect_success(prog, input, expected_sizes);
}

// ============================================================================
// Bitwise Operations
// ============================================================================

TEST_F(SDDL2CodeExecutionTest, BitwiseOperations)
{
    const std::vector<size_t> expected_sizes = { 4, 4 };
    std::vector<uint8_t> input               = {
        0xF0, 0x00, 0x00, 0x00, // a = 0x000000F0
        0x0F, 0x00, 0x00, 0x00, // b = 0x0000000F
    };

    const auto prog = R"(
        a: Int32LE
        b: Int32LE
        expect (a & b) == 0
        expect (a | b) == 255
        expect (a ^ b) == 255
        expect (~0) == -1
    )";

    expect_success(prog, input, expected_sizes);
}

} // namespace testing
} // namespace sddl2
} // namespace openzl
