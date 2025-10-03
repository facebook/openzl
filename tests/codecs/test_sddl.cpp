// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <random>

#include <gtest/gtest.h>

#include "openzl/compress/graphs/simple_data_description_language.h"
#include "openzl/compress/graphs/simple_data_description_language_source_code.h"

#include "openzl/codecs/zl_sddl.h"
#include "openzl/zl_reflection.h"

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/codecs/SDDL.hpp"
#include "openzl/cpp/detail/NonNullUniqueCPtr.hpp"

#include "tools/sddl/compiler/Compiler.h"
#include "tools/sddl/compiler/Exception.h"

#include "tests/utils.h"

using namespace ::testing;

namespace openzl {
namespace tests {

namespace {

detail::NonNullUniqueCPtr<ZL_SDDL_Program> make_prog()
{
    return detail::NonNullUniqueCPtr<ZL_SDDL_Program>(
            ZL_SDDL_Program_create(nullptr), ZL_SDDL_Program_free);
}

detail::NonNullUniqueCPtr<ZL_SDDL_State> make_state(const ZL_SDDL_Program* prog)
{
    return detail::NonNullUniqueCPtr<ZL_SDDL_State>(
            ZL_SDDL_State_create(prog, nullptr), ZL_SDDL_State_free);
}

std::string iota(size_t len)
{
    std::string ret;
    ret.resize(len);
    for (size_t i = 0; i < len; i++) {
        ret[i] = (char)(i + 1);
    }
    return ret;
}

class SimpleDataDescriptionLanguageTest : public Test {
   protected:
    enum class Expected {
        SUCCEED,
        FAIL_TO_COMPILE,
        FAIL_TO_DESERIALIZE,
        FAIL_TO_EXECUTE,
    };

    std::string compile(std::string_view program, Expected expected)
    {
        int verbosity = 2;
        std::stringstream logs;
        std::string code;

        try {
            code = sddl::Compiler{ logs, verbosity }.compile(
                    program, "[local_input]");
        } catch (const sddl::CompilerException&) {
            if (expected == Expected::FAIL_TO_COMPILE) {
                // Good.
            } else {
                EXPECT_TRUE(false)
                        << "Compilation threw when it shouldn't have!\n"
                        << "Compiler debug logs:\n"
                        << logs.str();
                throw;
            }
        } catch (...) {
            EXPECT_TRUE(false)
                    << "Compilation threw something else! Which it should never do.\n"
                    << "Compiler debug logs:\n"
                    << logs.str();
            throw;
        }

        // std::cerr << logs.str();

        return code;
    }

    void
    exec(std::string_view program, std::string_view input, Expected expected)
    {
        const auto code = compile(program, expected);

        auto prog = make_prog();
        {
            const auto res =
                    ZL_SDDL_Program_load(prog.get(), code.data(), code.size());
            ASSERT_EQ(
                    ZL_RES_isError(res),
                    expected == Expected::FAIL_TO_DESERIALIZE)
                    << ZL_SDDL_Program_getErrorContextString_fromError(
                               prog.get(), ZL_RES_error(res));
        }

        auto state = make_state(prog.get());
        {
            const auto res =
                    ZL_SDDL_State_exec(state.get(), input.data(), input.size());
            ASSERT_EQ(
                    ZL_RES_isError(res), expected == Expected::FAIL_TO_EXECUTE)
                    << ZL_SDDL_State_getErrorContextString_fromError(
                               state.get(), ZL_RES_error(res));
        }
    }

    void roundtrip(std::string_view program, std::string_view input)
    {
        const auto code = compile(program, Expected::SUCCEED);

        Compressor compressor;

        {
            compressor.setParameter(
                    CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
            compressor.setParameter(CParam::MinStreamSize, 1);

            const auto gid = graphs::SDDL(code, ZL_GRAPH_STORE)(compressor);
            EXPECT_TRUE(ZL_GraphID_isValid(gid));

            compressor.unwrap(
                    ZL_Compressor_selectStartingGraphID(compressor.get(), gid));
        }

        CCtx cctx;
        cctx.refCompressor(compressor);

        const auto compressed = cctx.compressSerial(input);

        DCtx dctx;
        const auto decompressed_output = dctx.decompressOne(compressed);
        const auto decompressed        = std::string_view(
                static_cast<const char*>(decompressed_output.ptr()),
                decompressed_output.contentSize());

        ASSERT_EQ(input, decompressed);
    }
};

} // anonymous namespace

TEST_F(SimpleDataDescriptionLanguageTest, DieIf2Plus2DoesntEqual4)
{
    const auto prog  = R"(
        two = 2;
        expect 2 + two == 4;
    )";
    const auto input = std::string_view{ "" };
    exec(prog, input, Expected::SUCCEED);
}

TEST_F(SimpleDataDescriptionLanguageTest, DieIf2Plus2Equals4)
{
    const auto prog  = R"(
        two = 2;
        expect 2 + two != 4;
    )";
    const auto input = std::string_view{ "" };
    exec(prog, input, Expected::FAIL_TO_EXECUTE);
}

TEST_F(SimpleDataDescriptionLanguageTest, TrivialRoundtrip)
{
    const auto prog   = R"(
        : Byte[_rem]
    )";
    const auto& input = zstrong::tests::kLoremTestInput;
    roundtrip(prog, input);
}

TEST_F(SimpleDataDescriptionLanguageTest, AlternateFields)
{
    const auto prog  = R"(
        field_width = 4;
        Field1 = Byte[field_width];
        Field2 = Byte[field_width];
        Row = {
            Field1;
            Field2;
        };
        row_width = sizeof Row;
        input_size = _rem;
        row_count = input_size / row_width;

        # check row size evenly divides input
        expect input_size % row_width == 0;

        RowArray = Row[row_count];
        : RowArray;
    )";
    const auto input = std::string_view{
        "1234567812345678123456781234567812345678123456781234567812345678"
        "1234567812345678123456781234567812345678123456781234567812345678"
        "1234567812345678123456781234567812345678123456781234567812345678"
        "1234567812345678123456781234567812345678123456781234567812345678"
        "1234567812345678123456781234567812345678123456781234567812345678"
        "1234567812345678123456781234567812345678123456781234567812345678"
        "1234567812345678123456781234567812345678123456781234567812345678"
        "1234567812345678123456781234567812345678123456781234567812345678"
    };
    roundtrip(prog, input);
}

TEST_F(SimpleDataDescriptionLanguageTest, SAO)
{
    const auto prog  = R"(
        # SAO Format Description:
        # http://tdc-www.harvard.edu/catalogs/catalogsb.html

        # Send all header fields to the same output
        HeaderInt = UInt32LE

        Header = {
            STAR0: HeaderInt
            STAR1: HeaderInt  # First star number in file
            STARN: HeaderInt  # Number of stars in file
            STNUM: HeaderInt  # star i.d. number presence
            MPROP: HeaderInt  # True if proper motion is included
            NMAG : HeaderInt  # Number of magnitudes present
            NBENT: HeaderInt  # Number of bytes per star entry
        }

        Row = {
            SRA0 : UInt64LE  # Right ascension in degrees
            SDEC0: UInt64LE  # Declination in degrees
            IS   : Byte[2]   # Instrument status flags
            MAG  : UInt16LE  # Magnitude * 100
            XRPM : UInt32LE  # X-axis rate per minute
            XDPM : UInt32LE  # X-axis drift per minute
        }

        # Read the header
        header: Header

        # Validate format expectations
        expect header.STNUM == 0
        expect header.MPROP == 1
        expect header.NMAG  == 1
        expect header.NBENT == sizeof Row

        # The header is followed by STARN records
        data: Row[header.STARN]

        # There should be no remaining input
        expect _rem == 0
    )";
    const auto input = std::string_view{
        "\x00\x00\x00\x00\x01\x00\x00\x00\x0a\x00\x00\x00\x00\x00\x00\x00"
        "\x01\x00\x00\x00\x01\x00\x00\x00\x1c\x00\x00\x00\xd4\xa7\xbb\x0b"
        "\xb7\x4a\x38\x3f\x6b\xa6\x15\xda\xc0\x17\xf7\x3f\x41\x30\xd0\x02"
        "\x99\x06\x22\xb5\xaa\x94\x26\x32\xb7\x4b\xf8\x98\x9f\xe4\x46\x3f"
        "\xd4\x50\x5f\x65\x5e\x57\xf6\x3f\x46\x32\x02\x03\x69\xe0\xd0\x35"
        "\x25\x24\x02\x34\x8e\x6d\xb5\x2c\xea\x23\x67\x3f\x16\xbb\xf7\xc5"
        "\x1e\x01\xf7\x3f\x20\x20\x98\x03\xab\xec\xce\xb4\x00\x00\x00\x00"
        "\xd6\xb0\x43\xef\x19\x0d\x68\x3f\xd4\x1a\x12\x51\xe1\x65\xf6\x3f"
        "\x20\x20\xa2\x03\x61\xf1\xf6\x35\x25\x24\x02\xb4\xba\xfa\x06\x30"
        "\x65\xf2\x6e\x3f\xa2\x4e\xaa\x42\xef\x77\xf6\x3f\x20\x20\x8e\x03"
        "\x06\x10\x72\x34\x8a\xd0\xc5\x33\x92\x28\xce\xae\xa9\x13\x72\x3f"
        "\x40\x81\x67\xb5\xc4\x0a\xf8\x3f\x46\x30\xa2\x03\xcb\xb0\x2f\xb5"
        "\xaa\x94\x26\xb2\x70\x41\x08\x65\x95\x85\x78\x3f\x42\x4d\xec\x1c"
        "\x76\xba\xf7\x3f\x20\x20\x98\x03\xd3\xe1\xa7\x34\x15\xc2\x11\x33"
        "\xf6\xc1\x5d\x12\x4a\x59\x7d\x3f\x2d\x63\x7e\x15\xfb\x82\xf7\x3f"
        "\x20\x20\xac\x03\xfb\xd6\x00\x35\xaa\x94\xa6\xb3\xe9\x6d\xb2\x81"
        "\x85\xf3\x81\x3f\x39\x7e\x0f\xcc\x20\x11\xf7\x3f\x20\x20\xb6\x03"
        "\xe3\x43\x98\x34\x3f\x67\x3b\xb3\x60\xcd\xb2\x13\x48\x13\x82\x3f"
        "\xf5\xf1\x68\xbd\xa2\x48\xf7\x3f\x20\x20\xac\x03\x37\x36\x43\xb5"
        "\xff\xde\x79\x32",
        308
    };
    roundtrip(prog, input);
}

TEST_F(SimpleDataDescriptionLanguageTest, consumeVals)
{
    const auto prog  = R"(
        B = Byte
        I1L = Int8
        I1B = Int8
        U1L = UInt8
        U1B = UInt8
        I2L = Int16LE
        I2B = Int16BE
        U2L = UInt16LE
        U2B = UInt16BE
        I4L = Int32LE
        I4B = Int32BE
        U4L = UInt32LE
        U4B = UInt32BE
        I8L = Int64LE
        I8B = Int64BE
        U8L = UInt64LE
        U8B = UInt64BE

        expect (:B) == 1
        expect (:B) == 254

        expect (:I1L) == 1
        expect (:I1L) == -2
        expect (:I1B) == 1
        expect (:I1B) == -2
        expect (:U1L) == 1
        expect (:U1L) == 239
        expect (:U1B) == 1
        expect (:U1B) == 239
        expect (:I2L) == 291
        expect (:I2L) == -292
        expect (:I2B) == 291
        expect (:I2B) == -292
        expect (:U2L) == 291
        expect (:U2L) == 61389
        expect (:U2B) == 291
        expect (:U2B) == 61389
        expect (:I4L) == 19088743
        expect (:I4L) == -19088744
        expect (:I4B) == 19088743
        expect (:I4B) == -19088744
        expect (:U4L) == 19088743
        expect (:U4L) == 4023233417
        expect (:U4B) == 19088743
        expect (:U4B) == 4023233417
        expect (:I8L) == 81985529216486895
        expect (:I8L) == -81985529216486896
        expect (:I8B) == 81985529216486895
        expect (:I8B) == -81985529216486896
        expect (:U8L) == 81985529216486895
        expect (:U8L) == 8056283915067138817
        expect (:U8B) == 81985529216486895
        expect (:U8B) == 8056283915067138817
    )";
    const auto input = std::string{
        "\x01"
        "\xfe"
        "\x01"
        "\xfe"
        "\x01"
        "\xfe"
        "\x01"
        "\xef"
        "\x01"
        "\xef"
        "\x23\x01"
        "\xdc\xfe"
        "\x01\x23"
        "\xfe\xdc"
        "\x23\x01"
        "\xcd\xef"
        "\x01\x23"
        "\xef\xcd"
        "\x67\x45\x23\x01"
        "\x98\xba\xdc\xfe"
        "\x01\x23\x45\x67"
        "\xfe\xdc\xba\x98"
        "\x67\x45\x23\x01"
        "\x89\xab\xcd\xef"
        "\x01\x23\x45\x67"
        "\xef\xcd\xab\x89"
        "\xef\xcd\xab\x89\x67\x45\x23\x01"
        "\x10\x32\x54\x76\x98\xba\xdc\xfe"
        "\x01\x23\x45\x67\x89\xab\xcd\xef"
        "\xfe\xdc\xba\x98\x76\x54\x32\x10"
        "\xef\xcd\xab\x89\x67\x45\x23\x01"
        "\x01\x23\x45\x67\x89\xab\xcd\x6f"
        "\x01\x23\x45\x67\x89\xab\xcd\xef"
        "\x6f\xcd\xab\x89\x67\x45\x23\x01"
    };
    roundtrip(prog, input);
}

TEST_F(SimpleDataDescriptionLanguageTest, mildlyVexingParses)
{
    const auto prog  = R"(
        b : B = Byte
        expect b == 1
        b = - : B
        expect b == -2
        : B
        b : B
        expect b == 4
        : B = Byte
        b : B
        expect b == 6
        : B
        A = B[--:B]
        : A
    )";
    const auto input = std::string{
        "\x01\x02\x03\x04\x05\x06\x07"
        "\x02"
        "\x01\x02"
    };
    roundtrip(prog, input);
}

TEST_F(SimpleDataDescriptionLanguageTest, exprEvalOrder)
{
    const auto prog  = R"(
        expect (:UInt16LE) + (:UInt16BE) + (:Byte) == (:Byte)
    )";
    const auto input = std::string{ "\x01\x00\x00\x02\x03\x06", 6 };
    roundtrip(prog, input);
}

TEST_F(SimpleDataDescriptionLanguageTest, recordsWithFieldNames)
{
    const auto prog  = R"(
        Foo = {
            Byte
            a : Byte
            : Byte
            b : Byte
        }

        foo : Foo

        expect foo.a == 2
        expect foo.b == 4
    )";
    const auto input = std::string{ "\x01\x02\x03\x04" };
    roundtrip(prog, input);
}

TEST_F(SimpleDataDescriptionLanguageTest, func)
{
    const auto prog  = R"(
        func = (arg1, arg2) {
            : Byte[arg1]
            a : Byte
            : Byte[arg2]
            b : Byte
        }

        foo : func(1, 1)
        bar : func(0, 2)

        expect foo.a == 2
        expect foo.b == 4
        expect bar.a == 5
        expect bar.b == 8
    )";
    const auto input = iota(8);
    roundtrip(prog, input);
}

TEST_F(SimpleDataDescriptionLanguageTest, funcPartialApplication)
{
    const auto prog  = R"(
        func = (arg1, arg2) {
            : Byte[arg1]
            a : Byte
            : Byte[arg2]
            b : Byte
        }

        partial_1 = func(1)
        partial_0 = func(0)

        partial_1_1 = partial_1(1)
        partial_0_2 = partial_0(2)

        foo : partial_1_1()

        # with no new args to bind, the parens are actually unnecessary
        bar : partial_0_2

        expect foo.a == 2
        expect foo.b == 4
        expect bar.a == 5
        expect bar.b == 8
    )";
    const auto input = iota(8);
    roundtrip(prog, input);
}

TEST_F(SimpleDataDescriptionLanguageTest, funcArgsComplexTypes)
{
    // This tests that we correctly track the lifetimes of function args
    const auto prog  = R"(
        f = (m, n) {
            : Byte[m]
            : Byte[n]
            val : Byte
        }

        g = (f, n) {
            r : f(n)
        }

        m = 1
        n = 1

        h = g(f(m), n)

        g = 0
        f = 0

        r : h

        expect r.r.val == m + n + 1
    )";
    const auto input = iota(3);
    roundtrip(prog, input);
}

TEST_F(SimpleDataDescriptionLanguageTest, avoidScopeCopiesInTemporaryFunctions)
{
    const auto prog  = R"(
        f : (a1, a2, a3, a4, a5) { : Byte } (1)(2)(3)(4)(5)
    )";
    const auto input = iota(1);
    roundtrip(prog, input);
}

TEST_F(SimpleDataDescriptionLanguageTest, directlyUseAggregateFieldDecls)
{
    const auto prog  = R"(
        : {}[1][1]
        : {Byte}[1][1]
        : {{Byte}}[1][1]
    )";
    const auto input = iota(2);
    roundtrip(prog, input);
}

TEST_F(SimpleDataDescriptionLanguageTest, consumeTooMuch)
{
    const auto program = R"(
        # error shouldn't include this line
        : Byte[10] # it should include this line
        # nor should it include this
    )";
    const auto input   = iota(1);

    const auto code = compile(program, Expected::SUCCEED);

    auto prog = make_prog();
    ASSERT_ZS_VALID(ZL_SDDL_Program_load(prog.get(), code.data(), code.size()));

    auto state = make_state(prog.get());
    const auto res =
            ZL_SDDL_State_exec(state.get(), input.data(), input.size());
    ASSERT_TRUE(ZL_RES_isError(res));

    const auto err_str =
            std::string{ ZL_SDDL_State_getErrorContextString_fromError(
                    state.get(), ZL_RES_error(res)) };

    EXPECT_NE(
            err_str.find(": Byte[10] # it should include this line"),
            std::string::npos)
            << err_str;
    EXPECT_EQ(
            err_str.find("# error shouldn't include this line"),
            std::string::npos)
            << err_str;
    EXPECT_EQ(err_str.find("# nor should it include this"), std::string::npos)
            << err_str;
}

TEST_F(SimpleDataDescriptionLanguageTest, indeterminateArrayLength)
{
    const auto program = R"(
        : UInt32LE[]
        expect _rem == 0
    )";

    for (size_t i = 4; i < 33; i++) {
        exec(program,
             iota(i),
             (i % 4) ? Expected::FAIL_TO_EXECUTE : Expected::SUCCEED);
    }

    // Zero-sized objects can't be expanded.
    exec(": {}[]; :Byte[3]", iota(3), Expected::FAIL_TO_EXECUTE);
    exec(": Byte[0][]; :Byte[3]", iota(3), Expected::FAIL_TO_EXECUTE);
}

class SimpleDataDescriptionLanguageSourceCodePrettyPrintingTest : public Test {
   protected:
    void SetUp() override
    {
        arena_ = ALLOC_HeapArena_create();
    }

    void TearDown() override
    {
        ALLOC_Arena_freeArena(arena_);
    }

    Arena* arena_;
};

TEST_F(SimpleDataDescriptionLanguageSourceCodePrettyPrintingTest, RandomStrings)
{
    std::mt19937 randgen{ 1 };
    const std::string alphabet{ "abcdefghijklmnopqrstuvwxyz \n" };
    std::uniform_int_distribution<size_t> alphabet_dist{ 0,
                                                         alphabet.size() - 1 };
    for (size_t i = 0; i < 10000; i++) {
        std::string src;
        const auto src_len = std::uniform_int_distribution{ 0, 1000 }(randgen);
        src.reserve(src_len);
        for (size_t j = 0; j < src_len; j++) {
            src += alphabet[alphabet_dist(randgen)];
        }

        ZL_SDDL_SourceCode sc;
        ZL_SDDL_SourceCode_init(
                arena_, &sc, StringView_init(src.data(), src.size()));

        ZL_SDDL_SourceLocation sl;
        sl.start = std::uniform_int_distribution{ 0, src_len }(randgen);
        sl.size =
                std::uniform_int_distribution<size_t>{ 0, src_len - sl.start }(
                        randgen);

        const auto indent = std::uniform_int_distribution{ 0, 10 }(randgen);

        const auto pstr_res = ZL_SDDL_SourceLocationPrettyString_create(
                nullptr, arena_, &sc, &sl, indent);
        if (ZL_RES_isError(pstr_res)) {
            std::cerr << ZL_E_str(ZL_RES_error(pstr_res)) << std::endl;
        }
        ASSERT_ZS_VALID(pstr_res);

        const auto& pstr = ZL_RES_value(pstr_res);

        ZL_SDDL_SourceLocationPrettyString_destroy(arena_, &pstr);
        ZL_SDDL_SourceCode_destroy(arena_, &sc);
    }
}

} // namespace tests
} // namespace openzl
