// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <optional>

#include <folly/Range.h>
#include <folly/io/IOBuf.h>
#include <thrift/lib/cpp2/protocol/Serializer.h>

#include "security/lionhead/utils/lib_ftest/enable_sfdp_thrift.h"
#include "security/lionhead/utils/lib_ftest/ftest.h"

#include "custom_transforms/thrift/kernels/decode_thrift_binding.h"
#include "custom_transforms/thrift/kernels/encode_thrift_binding.h"
#include "custom_transforms/thrift/kernels/tests/thrift_kernel_test_utils.h"
#include "openzl/common/logging.h"
#include "tests/fuzz_utils.h"
#include "tests/zstrong/test_zstrong_fixture.h"

namespace openzl::thrift::tests {
using namespace openzl::tests;
using ThriftKernelData = zstrong::thrift::tests::ThriftKernelData;

enum class TransformIDs {
    MapI32Float,
    MapI32ArrayFloat,
    MapI32ArrayI64,
    MapI32ArrayArrayI64,
    MapI32MapI64Float,
    ArrayI64,
    ArrayI32,
    ArrayFloat,
};

class ThriftKernelTest : public ZStrongTest {
   public:
    void SetUp() override
    {
        reset();
        setLargeCompressBound(8);

        nodes_[(unsigned)TransformIDs::MapI32Float] =
                ZS2_ThriftKernel_registerCTransformMapI32Float(
                        cgraph_, (unsigned)TransformIDs::MapI32Float);
        nodes_[(unsigned)TransformIDs::MapI32ArrayFloat] =
                ZS2_ThriftKernel_registerCTransformMapI32ArrayFloat(
                        cgraph_, (unsigned)TransformIDs::MapI32ArrayFloat);
        nodes_[(unsigned)TransformIDs::MapI32ArrayI64] =
                ZS2_ThriftKernel_registerCTransformMapI32ArrayI64(
                        cgraph_, (unsigned)TransformIDs::MapI32ArrayI64);
        nodes_[(unsigned)TransformIDs::MapI32ArrayArrayI64] =
                ZS2_ThriftKernel_registerCTransformMapI32ArrayArrayI64(
                        cgraph_, (unsigned)TransformIDs::MapI32ArrayArrayI64);
        nodes_[(unsigned)TransformIDs::MapI32MapI64Float] =
                ZS2_ThriftKernel_registerCTransformMapI32MapI64Float(
                        cgraph_, (unsigned)TransformIDs::MapI32MapI64Float);
        nodes_[(unsigned)TransformIDs::ArrayI64] =
                ZS2_ThriftKernel_registerCTransformArrayI64(
                        cgraph_, (unsigned)TransformIDs::ArrayI64);
        nodes_[(unsigned)TransformIDs::ArrayI32] =
                ZS2_ThriftKernel_registerCTransformArrayI32(
                        cgraph_, (unsigned)TransformIDs::ArrayI32);
        nodes_[(unsigned)TransformIDs::ArrayFloat] =
                ZS2_ThriftKernel_registerCTransformArrayFloat(
                        cgraph_, (unsigned)TransformIDs::ArrayFloat);

        ZL_REQUIRE_SUCCESS(ZS2_ThriftKernel_registerDTransformMapI32Float(
                dctx_, (unsigned)TransformIDs::MapI32Float));
        ZL_REQUIRE_SUCCESS(ZS2_ThriftKernel_registerDTransformMapI32ArrayFloat(
                dctx_, (unsigned)TransformIDs::MapI32ArrayFloat));
        ZL_REQUIRE_SUCCESS(ZS2_ThriftKernel_registerDTransformMapI32ArrayI64(
                dctx_, (unsigned)TransformIDs::MapI32ArrayI64));
        ZL_REQUIRE_SUCCESS(
                ZS2_ThriftKernel_registerDTransformMapI32ArrayArrayI64(
                        dctx_, (unsigned)TransformIDs::MapI32ArrayArrayI64));
        ZL_REQUIRE_SUCCESS(ZS2_ThriftKernel_registerDTransformMapI32MapI64Float(
                dctx_, (unsigned)TransformIDs::MapI32MapI64Float));
        ZL_REQUIRE_SUCCESS(ZS2_ThriftKernel_registerDTransformArrayI64(
                dctx_, (unsigned)TransformIDs::ArrayI64));
        ZL_REQUIRE_SUCCESS(ZS2_ThriftKernel_registerDTransformArrayI32(
                dctx_, (unsigned)TransformIDs::ArrayI32));
        ZL_REQUIRE_SUCCESS(ZS2_ThriftKernel_registerDTransformArrayFloat(
                dctx_, (unsigned)TransformIDs::ArrayFloat));
    }

    void testRoundTrip(std::map<int32_t, float> const& data)
    {
        testRoundTrip(
                nodes_[(unsigned)TransformIDs::MapI32Float], serialize(data));
    }

    void testRoundTrip(std::map<int32_t, std::vector<float>> const& data)
    {
        testRoundTrip(
                nodes_[(unsigned)TransformIDs::MapI32ArrayFloat],
                serialize(data));
    }

    void testRoundTrip(std::map<int32_t, std::vector<int64_t>> const& data)
    {
        testRoundTrip(
                nodes_[(unsigned)TransformIDs::MapI32ArrayI64],
                serialize(data));
    }

    void testRoundTrip(
            std::map<int32_t, std::vector<std::vector<int64_t>>> const& data)
    {
        testRoundTrip(
                nodes_[(unsigned)TransformIDs::MapI32ArrayArrayI64],
                serialize(data));
    }

    void testRoundTrip(std::map<int32_t, std::map<int64_t, float>> const& data)
    {
        testRoundTrip(
                nodes_[(unsigned)TransformIDs::MapI32MapI64Float],
                serialize(data));
    }

    void testRoundTrip(std::vector<int64_t> const& data)
    {
        testRoundTrip(
                nodes_[(unsigned)TransformIDs::ArrayI64], serialize(data));
    }

    void testRoundTrip(std::vector<int32_t> const& data)
    {
        testRoundTrip(
                nodes_[(unsigned)TransformIDs::ArrayI32], serialize(data));
    }

    void testRoundTrip(std::vector<float> const& data)
    {
        testRoundTrip(
                nodes_[(unsigned)TransformIDs::ArrayFloat], serialize(data));
    }

    void testRoundTrip(ThriftKernelData const& data)
    {
        apache::thrift::visit_union(
                data, [this](const auto&, const auto& value) {
                    testRoundTrip(value);
                });
    }

    using ZStrongTest::testRoundTrip;

    void testRoundTrip(ZL_NodeID node, std::string_view input)
    {
        finalizeGraph(declareGraph(node), 1);
        testRoundTrip(input);
    }

    void testCompress(ZL_NodeID node, std::string_view input)
    {
        finalizeGraph(declareGraph(node), 1);
        auto const [cReport, compressed] = compress(input);
        if (ZL_isError(cReport)) {
            // Allow compression to fail
            return;
        }

        // But if it succeeds we must round trip
        ZL_REQUIRE(compressed.has_value());
        auto const [dReport, decompressed] = decompress(*compressed);
        ZL_REQUIRE_SUCCESS(dReport);
        ZL_REQUIRE(decompressed.has_value());
        ZL_REQUIRE(input == decompressed.value());
    }

    void testDecompress(std::string_view input)
    {
        ZL_g_logLevel = ZL_LOG_LVL_ALWAYS;
        ZL_Report const dstSizeR =
                ZL_getDecompressedSize(input.data(), input.size());
        if (ZL_isError(dstSizeR))
            return;
        size_t const dstSize = ZL_validResult(dstSizeR);
        size_t const maxDstSize =
                std::min<size_t>(10 << 20, input.size() * 100);
        size_t const dstCapacity = std::min<size_t>(dstSize, maxDstSize);
        auto dst                 = std::make_unique<uint8_t[]>(dstCapacity);
        ZL_Report const report   = ZL_DCtx_decompress(
                dctx_, dst.get(), dstCapacity, input.data(), input.size());
        if (ZL_isError(report) && report._code == ZL_ErrorCode_logicError) {
            ZL_REQUIRE_SUCCESS(report);
        }
    }

    std::array<ZL_NodeID, 8> nodes_;
};

FUZZ_F(ThriftKernelTest, FuzzRoundTrip)
{
    // Ensure that we round trip valid thrift successfully
    auto input = f.template thrift<ThriftKernelData>("thrift_kernel_data");
    testRoundTrip(input);
}

FUZZ_F(ThriftKernelTest, FuzzCompress)
{
    // Ensure that compression doesn't crash on invalid input
    // And if compression succeeds, we round trip successfully
    std::string input = gen_str(f, "input_data", InputLengthInBytes(1));
    auto node         = f.choices("thrift_kernel_node", nodes_);
    testCompress(node, input);
}

FUZZ_F(ThriftKernelTest, FuzzDecompress)
{
    // Ensure decompression doesn't crash on corrupted data
    std::string input = gen_str(f, "input_data", InputLengthInBytes(1));
    testDecompress(input);
}

} // namespace openzl::thrift::tests
