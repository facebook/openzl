// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#include <limits>
#include <string>

#include <gtest/gtest.h>

#include "tests/compress/graphs/sddl2/utils.h"

#include "tools/sddl2/assembler/Assembler.h"
#include "tools/sddl2/compiler/Compiler.h"

#include "openzl/compress/graphs/sddl2/sddl2.h"
#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/CompressIntrospectionHooks.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/DCtx.hpp"
#include "openzl/cpp/DecompressIntrospectionHooks.hpp"
#include "openzl/cpp/codecs/SDDL2.hpp"
#include "openzl/zl_compress.h"
#include "openzl/zl_config.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_version.h"

namespace openzl {
namespace sddl2 {
namespace testing {

class SDDL2GraphTest : public SDDL2TestBase {
   protected:
    auto assemble_bytecode(const std::string& code)
    {
        auto assembly = compiler_.compile(code, "[test code]");
        return assembler_.assemble(std::move(assembly));
    }

    template <typename T>
    void expect_success(const std::string& code, const std::vector<T>& input)
    {
        poly::string_view input_view(
                (const char*)input.data(), input.size() * sizeof(T));

        auto compressed   = compress(code, input_view, 0);
        auto decompressed = decompress(compressed);

        EXPECT_EQ(input_view, decompressed);
    }

    template <typename T>
    void expect_explicit_zero_chunk_param_success(
            const std::string& code,
            const std::vector<T>& input)
    {
        poly::string_view input_view(
                (const char*)input.data(), input.size() * sizeof(T));

        auto compressed =
                compress_with_explicit_chunk_param(code, input_view, 0);
        auto decompressed = decompress(compressed);

        EXPECT_EQ(input_view, decompressed);
    }

    template <typename T>
    void expect_chunked_success(
            const std::string& code,
            const std::vector<T>& input,
            size_t chunk_size)
    {
        poly::string_view input_view(
                (const char*)input.data(), input.size() * sizeof(T));

        auto compressed   = compress(code, input_view, chunk_size);
        auto decompressed = decompress(compressed);

        EXPECT_EQ(input_view, decompressed);
    }

    template <typename T>
    void expect_chunked_error(
            const std::string& code,
            const std::vector<T>& input,
            size_t chunk_size,
            const std::string& msg)
    {
        poly::string_view input_view(
                (const char*)input.data(), input.size() * sizeof(T));

        try {
            (void)compress(code, input_view, chunk_size);
        } catch (const openzl::Exception& ex) {
            EXPECT_NE(std::string{ ex.what() }.find(msg), std::string::npos)
                    << ex.what();
            return;
        }
        EXPECT_TRUE(false) << "Should have thrown an openzl::Exception!"
                           << std::endl;
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
            compress(code, input_view, 0);
        } catch (const openzl::Exception& ex) {
            EXPECT_NE(std::string{ ex.what() }.find(msg), std::string::npos)
                    << ex.what();
            return;
        }
        EXPECT_TRUE(false) << "Should have thrown an openzl::Exception!"
                           << std::endl;
    }

#if ZL_ALLOW_INTROSPECTION
    template <typename T>
    void expect_chunk_count(
            const std::string& code,
            const std::vector<T>& input,
            size_t expected_chunks,
            size_t chunk_size  = 0,
            int format_version = ZL_MAX_FORMAT_VERSION)
    {
        poly::string_view input_view(
                (const char*)input.data(), input.size() * sizeof(T));

        CompressChunkCounterHook compress_hook;
        auto compressed = compress(
                code, input_view, chunk_size, format_version, &compress_hook);
        EXPECT_EQ(compress_hook.chunkCount, expected_chunks);

        DecompressChunkCounterHook decompress_hook;
        auto decompressed = decompress(compressed, &decompress_hook);
        EXPECT_EQ(decompress_hook.chunkCount, expected_chunks);
        EXPECT_EQ(input_view, decompressed);
    }

    template <typename T>
    void expect_explicit_chunk_param_chunk_count(
            const std::string& code,
            const std::vector<T>& input,
            int chunk_size_param,
            size_t expected_chunks)
    {
        poly::string_view input_view(
                (const char*)input.data(), input.size() * sizeof(T));

        CompressChunkCounterHook compress_hook;
        auto compressed = compress_with_explicit_chunk_param(
                code,
                input_view,
                chunk_size_param,
                ZL_MAX_FORMAT_VERSION,
                &compress_hook);
        EXPECT_EQ(compress_hook.chunkCount, expected_chunks);

        DecompressChunkCounterHook decompress_hook;
        auto decompressed = decompress(compressed, &decompress_hook);
        EXPECT_EQ(decompress_hook.chunkCount, expected_chunks);
        EXPECT_EQ(input_view, decompressed);
    }
#endif

   private:
    std::string compress_with_graph(
            ZL_GraphID graphID,
            const poly::string_view& input,
            int format_version                        = ZL_MAX_FORMAT_VERSION,
            openzl::CompressIntrospectionHooks* hooks = nullptr)
    {
        compressor_.selectStartingGraph(graphID);

        CCtx cctx;
        cctx.setParameter(CParam::FormatVersion, format_version);
        if (hooks != nullptr) {
            ZL_Report const attachr = ZL_CCtx_attachIntrospectionHooks(
                    cctx.get(), hooks->getRawHooks());
            EXPECT_FALSE(ZL_isError(attachr));
            if (ZL_isError(attachr)) {
                return {};
            }
        }
        cctx.refCompressor(compressor_);

        return cctx.compressSerial(input);
    }

    std::string compress(
            const std::string& code,
            const poly::string_view& input,
            size_t chunk_size,
            int format_version                        = ZL_MAX_FORMAT_VERSION,
            openzl::CompressIntrospectionHooks* hooks = nullptr)
    {
        auto bytecode = assemble_bytecode(code);

        const poly::string_view bytecode_view(
                (const char*)bytecode.data(), bytecode.size());

        // In this helper, chunk_size == 0 means "use the default 16 MiB
        // chunking behavior".
        auto graphID = chunk_size == 0
                ? graphs::SDDL2(bytecode_view, ZL_GRAPH_STORE)
                          .parameterize(compressor_)
                : graphs::SDDL2(bytecode_view, ZL_GRAPH_STORE, chunk_size)
                          .parameterize(compressor_);
        return compress_with_graph(graphID, input, format_version, hooks);
    }

    std::string compress_with_explicit_chunk_param(
            const std::string& code,
            const poly::string_view& input,
            int chunk_size_param,
            int format_version                        = ZL_MAX_FORMAT_VERSION,
            openzl::CompressIntrospectionHooks* hooks = nullptr)
    {
        auto bytecode = assemble_bytecode(code);

        LocalParams local_params;
        local_params.addCopyParam(
                SDDL2_BYTECODE_PARAM, bytecode.data(), bytecode.size());
        local_params.addIntParam(SDDL2_CHUNK_BYTE_SIZE_PARAM, chunk_size_param);

        auto graphID = compressor_.parameterizeGraph(
                ZL_GRAPH_SDDL2,
                {
                        .customGraphs = { { ZL_GRAPH_STORE } },
                        .localParams  = std::move(local_params),
                });
        return compress_with_graph(graphID, input, format_version, hooks);
    }

    std::string decompress(
            const std::string& input,
            openzl::DecompressIntrospectionHooks* hooks = nullptr)
    {
        DCtx dctx;
        if (hooks != nullptr) {
            ZL_Report const attachr =
                    ZL_DCtx_attachDecompressIntrospectionHooks(
                            dctx.get(), hooks->getRawHooks());
            EXPECT_FALSE(ZL_isError(attachr));
            if (ZL_isError(attachr)) {
                return {};
            }
        }
        return dctx.decompressSerial(input);
    }

    Assembler assembler_;
    Compiler compiler_;
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

TEST_F(SDDL2GraphTest, CompressChunkedRepeatedBlocks)
{
    constexpr size_t kBlocks          = 32;
    constexpr size_t kEntriesPerBlock = 256;
    constexpr size_t kChunkSize       = 32 << 10;

    std::string code = R"(
        Record Header() = {
            magic: UInt32LE,
            flag: Byte,
        }

        Record Entry(flag) = {
            id: UInt32LE,
            when flag {
                optional: UInt16LE,
            },
            required: UInt64LE,
        }

        header: Header
        expect header.magic == 0xdeadbeef
    )";
    for (size_t i = 0; i < kBlocks; ++i) {
        code += "block" + std::to_string(i) + ": Entry(header.flag)["
                + std::to_string(kEntriesPerBlock) + "]\n";
    }

    std::vector<uint8_t> input;
    input.reserve(5 + kBlocks * kEntriesPerBlock * 14);
    auto appendLE = [&input](uint64_t value, size_t width) {
        for (size_t i = 0; i < width; ++i) {
            input.push_back((uint8_t)(value >> (8 * i)));
        }
    };

    appendLE(0xdeadbeefULL, 4);
    input.push_back(1);
    for (size_t block = 0; block < kBlocks; ++block) {
        for (size_t entry = 0; entry < kEntriesPerBlock; ++entry) {
            const uint64_t value = block * 1000 + entry;
            appendLE(value, 4);
            appendLE(value ^ 0x55AA, 2);
            appendLE(value * 17 + 3, 8);
        }
    }

    expect_chunked_success(code, input, kChunkSize);
}

TEST_F(SDDL2GraphTest, CompressChunkedSingleLargeSegment)
{
    constexpr size_t kChunkSize  = ZL_MIN_CHUNK_SIZE;
    constexpr size_t kNumEntries = 3 * (kChunkSize / sizeof(uint32_t)) + 7;

    std::vector<uint32_t> input(kNumEntries);
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = (uint32_t)(i * 17 + 3);
    }

    expect_chunked_success(
            R"(
                payload : UInt32LE[]
            )",
            input,
            kChunkSize);
}

TEST_F(SDDL2GraphTest, CompressChunkedSingleLargeStructureSegment)
{
    constexpr size_t kChunkSize = ZL_MIN_CHUNK_SIZE;
    constexpr size_t kRecordSize =
            sizeof(uint8_t) + sizeof(int16_t) + sizeof(int32_t);
    constexpr size_t kRecordsPerChunk = kChunkSize / kRecordSize;
    constexpr size_t kNumRecords      = 3 * kRecordsPerChunk + 7;

    const auto input       = gen<uint8_t>(kNumRecords * kRecordSize);
    const std::string code = R"(
                payload : Record() {
                    a : Byte,
                    b : Int16LE,
                    c : Int32LE,
                }[]
            )";

    expect_chunked_success(code, input, kChunkSize);

#if ZL_ALLOW_INTROSPECTION
    expect_chunk_count(code, input, 4, kChunkSize);
#endif
}

#if ZL_ALLOW_INTROSPECTION
TEST_F(SDDL2GraphTest, ChunkSizeHintIgnoredBeforeChunkFormatVersion)
{
    constexpr size_t kChunkSize        = ZL_MIN_CHUNK_SIZE;
    constexpr size_t kExpectedChunked  = 4;
    constexpr size_t kLegacyChunkCount = 1;
    constexpr size_t kNumEntries = 3 * (kChunkSize / sizeof(uint32_t)) + 7;

    const auto input       = gen<uint32_t>(kNumEntries);
    const std::string code = R"(
                payload : UInt32LE[]
            )";

    expect_chunk_count(code, input, kExpectedChunked, kChunkSize);
    expect_chunk_count(
            code,
            input,
            kLegacyChunkCount,
            kChunkSize,
            ZL_CHUNK_VERSION_MIN - 1);
}

TEST_F(SDDL2GraphTest, ChunkCountDoesNotBackfillAcrossLargeSegmentBoundary)
{
    constexpr size_t kHeaderSize     = 8;
    constexpr size_t kRecords        = 10921;
    constexpr size_t kRecordSize     = sizeof(uint32_t) + sizeof(uint16_t);
    constexpr size_t kTrailerSize    = 16;
    constexpr size_t kChunkSize      = ZL_MIN_CHUNK_SIZE;
    constexpr size_t kExpectedChunks = 4;

    std::vector<uint8_t> input;
    input.reserve(kHeaderSize + kRecords * kRecordSize + kTrailerSize);
    for (size_t i = 0; i < kHeaderSize; ++i) {
        input.push_back((uint8_t)i);
    }
    for (size_t i = 0; i < kRecords; ++i) {
        uint32_t id = (uint32_t)(i * 11 + 7);
        input.push_back((uint8_t)(id >> 0));
        input.push_back((uint8_t)(id >> 8));
        input.push_back((uint8_t)(id >> 16));
        input.push_back((uint8_t)(id >> 24));
        uint16_t value = (uint16_t)(i ^ 0x55AA);
        input.push_back((uint8_t)(value >> 0));
        input.push_back((uint8_t)(value >> 8));
    }
    for (size_t i = 0; i < kTrailerSize; ++i) {
        input.push_back((uint8_t)(0xF0 + i));
    }

    std::string code = R"(
                header : Bytes(8)
                records : Record() {
                    id : UInt32LE,
                    value : UInt16LE,
                }[)";
    code += std::to_string(kRecords);
    code += R"(]
                trailer : Bytes(16)
            )";

    expect_chunk_count(code, input, kExpectedChunks, kChunkSize);
}

TEST_F(SDDL2GraphTest, DefaultChunkSizeAppliedWhenHintOmitted)
{
    constexpr size_t kNumEntries =
            SDDL2_DEFAULT_CHUNK_BYTE_SIZE / sizeof(uint32_t) + 7;

    std::vector<uint32_t> input(kNumEntries);
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = (uint32_t)(i * 17 + 3);
    }

    expect_chunk_count(
            R"(
                payload : UInt32LE[]
            )",
            input,
            2);
}

TEST_F(SDDL2GraphTest, ExplicitZeroChunkHintUsesDefaultChunking)
{
    constexpr size_t kNumEntries =
            SDDL2_DEFAULT_CHUNK_BYTE_SIZE / sizeof(uint32_t) + 7;

    std::vector<uint32_t> input(kNumEntries);
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = (uint32_t)(i * 17 + 3);
    }

    expect_explicit_chunk_param_chunk_count(
            R"(
                payload : UInt32LE[]
            )",
            input,
            0,
            2);
}
#endif

TEST_F(SDDL2GraphTest, SimpleApiOmitsDefaultChunkSizeParam)
{
    auto bytecode = assemble_bytecode(
            R"(
                payload : UInt32LE[]
            )");

    Compressor simple_api_compressor;
    auto simple_graph = ZL_Compressor_registerSDDL2Graph(
            simple_api_compressor.get(), bytecode.data(), bytecode.size());
    ASSERT_NE(simple_graph, ZL_GRAPH_ILLEGAL);
    simple_api_compressor.selectStartingGraph(simple_graph);

    Compressor omitted_param_compressor;
    LocalParams local_params;
    local_params.addCopyParam(
            SDDL2_BYTECODE_PARAM, bytecode.data(), bytecode.size());
    auto omitted_param_graph = omitted_param_compressor.parameterizeGraph(
            ZL_GRAPH_SDDL2,
            {
                    .localParams = std::move(local_params),
            });
    omitted_param_compressor.selectStartingGraph(omitted_param_graph);

    EXPECT_EQ(
            simple_api_compressor.serializeToJson(),
            omitted_param_compressor.serializeToJson());
}

TEST_F(SDDL2GraphTest, OversizedChunkHintThrowsAtConstruction)
{
    const size_t chunk_size =
            static_cast<size_t>(std::numeric_limits<int>::max()) + 1;

    try {
        (void)graphs::SDDL2(
                poly::string_view("", 0), ZL_GRAPH_STORE, chunk_size);
        FAIL() << "Expected oversized chunk size to throw at construction";
    } catch (const openzl::Exception& ex) {
        EXPECT_NE(
                std::string{ ex.what() }.find("Bad SDDL2 chunk size"),
                std::string::npos)
                << ex.what();
    }
}

TEST_F(SDDL2GraphTest, CompressWithExplicitZeroChunkLocalParam)
{
    const size_t star_entry_size = 2 * sizeof(double) + 2 * sizeof(uint8_t)
            + sizeof(int16_t) + 2 * sizeof(float);
    const auto input = gen<uint8_t>(28 + 100 * star_entry_size);

    expect_explicit_zero_chunk_param_success(
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

TEST_F(SDDL2GraphTest, ChunkedCompressInputSizeNotMultipleOfRecordSize)
{
    constexpr size_t kChunkSize = ZL_MIN_CHUNK_SIZE;
    constexpr size_t kRecords = ZL_MIN_CHUNK_SIZE / (2 * sizeof(int32_t)) + 100;
    const auto input = gen<uint8_t>(2 * sizeof(int32_t) * kRecords + 1);

    expect_chunked_error(
            R"(
                : Record() {
                    a : Int32LE,
                    b : Int32LE,
                }[]
            )",
            input,
            kChunkSize,
            "split instructions do not map exactly the entire input");
}

TEST_F(SDDL2GraphTest, ChunkedCompressElementLargerThanChunkSize)
{
    const auto input = gen<uint32_t>(4);

    expect_chunked_error(
            R"(
                payload : UInt32LE[]
            )",
            input,
            2,
            "cannot fit one element of size 4");
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
