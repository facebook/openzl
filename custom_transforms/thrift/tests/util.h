// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "custom_transforms/thrift/binary_splitter.h"
#include "custom_transforms/thrift/compact_splitter.h"
#include "custom_transforms/thrift/debug.h"
#include "custom_transforms/thrift/kernels/tests/thrift_kernel_test_utils.h"
#include "custom_transforms/thrift/parse_config.h"
#include "custom_transforms/thrift/split_helpers.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_version.h"
#include "tests/datagen/random_producer/RNGEngine.h"
#include "tests/datagen/structures/FixedWidthDataProducer.h"
#include "tools/zstrong_cpp.h"

#if ZL_FBCODE_IS_RELEASE
#    include "openzl/prod/custom_transforms/thrift/tests/gen-cpp2/test_schema_types.h"
#    include "openzl/prod/custom_transforms/thrift/tests/gen-cpp2/test_schema_visitation.h"
#else
#    include "openzl/dev/custom_transforms/thrift/tests/gen-cpp2/test_schema_types.h"
#    include "openzl/dev/custom_transforms/thrift/tests/gen-cpp2/test_schema_visitation.h"
#endif

#include <folly/Range.h>
#include <optional>
#include <string>

#pragma once

namespace openzl::thrift::tests {

using namespace zstrong::thrift;

// Works for any ZL_VOEncoderDesc which takes Thrift splitter localParams.
// In other words, this function works equally well for TCompact and TBinary.
std::string thriftSplitCompress(
        const ZL_VOEncoderDesc& compress,
        std::string_view src,
        std::string_view serializedConfig,
        const int formatVersion);

// TODO(T171457232) use this to deprecate the Python walker
template <typename Parser>
WriteStreamSet thriftSplitIntoWriteStreams(
        std::string_view src,
        std::string_view serializedConfig)
{
    EncoderConfig const config = EncoderConfig(serializedConfig);
    ReadStream srcStream{ folly::ByteRange{ src } };
    WriteStreamSet dstStreamSet{ config, ZL_MAX_FORMAT_VERSION };
    Parser parser{ config, srcStream, dstStreamSet, ZL_MAX_FORMAT_VERSION };
    parser.parse();
    debug("Encoder side:");
    debug(srcStream.repr());
    debug(dstStreamSet.repr());
    return dstStreamSet;
}

// Same as above, this function works equally well for TCompact and TBinary.
// Checks that *compressed* decodes to *original*.
void thriftSplitDecompress(
        std::string_view compressed,
        std::optional<std::string_view> original);

// Helper function to compress & decompress
void runThriftSplitterRoundTrip(
        const ZL_VOEncoderDesc& compress,
        std::string_view src,
        std::string_view serializedConfig,
        int minFormatVersion = kMinFormatVersionEncode,
        int maxFormatVersion = ZL_MAX_FORMAT_VERSION);

//////////////////////// PARSE CONFIG GENERATION UTILS ////////////////////////

// Builds an encoder config which is expected to succeed compression on
// cpp2::TestStruct data. A seed can be provided to randomize the config.
//
// Long-term we should move away from the tight coupling with cpp2::TestStruct.
// I'm tracking that at T171457232.

enum class ConfigGenMode {
    // In this mode, random choices are constrained to ensure a high
    // level of coverage. This is useful when we want to maximize the coverage
    // of a single config (e.g. in our version compatibility tests).
    kMoreCoverage,

    // In this mode, random choices are less constrained, but the
    // coverage of an individual config may be lower. This is useful when we
    // want to test with many configs from many different seeds.
    kMoreFreedom
};

// When this seed is used in kMoreCoverage mode, we assert() that the config has
// high coverage of different parser features.
int constexpr kDefaultConfigSeed = 0xdeadbeef;

std::string buildValidEncoderConfig(
        int minFormatVersion,
        int seed             = kDefaultConfigSeed,
        ConfigGenMode mode   = ConfigGenMode::kMoreCoverage,
        int maxFormatVersion = ZL_MAX_FORMAT_VERSION);

///////////////////////////////////////////////////////////////////////////////

template <typename Serializer, typename RNG>
std::string generateRandomThrift(RNG&& gen)
{
    const auto testStruct = generate<zstrong::thrift::tests::cpp2::TestStruct>(
            std::forward<RNG>(gen));
    return Serializer::template serialize<std::string>(testStruct);
}

inline size_t thriftSplitCompressBound(size_t srcSize, size_t configSize)
{
    return (10 * srcSize) + ZL_compressBound(srcSize + configSize);
}

template <typename Serializer>
class ConfigurableThriftProducer
        : public openzl::tests::datagen::FixedWidthDataProducer {
   public:
    explicit ConfigurableThriftProducer(
            std::shared_ptr<openzl::tests::datagen::RandWrapper> rw)
            : FixedWidthDataProducer(std::move(rw), 1)
    {
    }

    ::openzl::tests::datagen::FixedWidthData operator()(
            openzl::tests::datagen::RandWrapper::NameType) override
    {
        return {
            generateRandomThrift<Serializer>(
                    openzl::tests::datagen::RNGEngine<uint32_t>(
                            this->rw_.get(),
                            "ConfigurableThriftProducer::RNG::operator()")),
            1
        };
    }

    void print(std::ostream& os) const override
    {
        os << "ConfigurableThriftProducer(std::string, 1)";
    }
};

} // namespace openzl::thrift::tests
