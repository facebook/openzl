// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "contrib/gpu/src/codecs/float_deconstruct/tests/bf16_test_frames.h"

#include <gtest/gtest.h>

#include <memory>
#include <random>
#include <string>

#include "contrib/gpu/testkit/frame_factory.h"
#include "openzl/codecs/float_deconstruct/common_float_deconstruct_binding.h"
#include "openzl/common/wire_format.h"
#include "openzl/openzl.hpp"
#include "openzl/zl_reflection.h"

namespace openzl::gpu::test {

const std::byte kBf16Header[1] = { std::byte{ FLTDECON_ElementType_bfloat16 } };

std::vector<uint16_t> makeBf16Values(size_t numElts, unsigned seed)
{
    std::mt19937 rng(seed);
    std::uniform_int_distribution<uint32_t> dist(0, 0xFFFF);
    std::vector<uint16_t> values(numElts);
    for (uint16_t& value : values) {
        value = static_cast<uint16_t>(dist(rng));
    }
    return values;
}

Bf16Streams encodeBf16(const std::vector<uint16_t>& values)
{
    Compressor compressor;
    compressor.setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
    const std::string frame = testkit::makeFrameNodeToStore(
            compressor,
            nodes::BFloat16Deconstruct::node,
            Input::refNumeric<uint16_t>(values.data(), values.size()));

    const std::unique_ptr<ZL_ReflectionCtx, void (*)(ZL_ReflectionCtx*)> rctx{
        ZL_ReflectionCtx_create(), ZL_ReflectionCtx_free
    };
    if (rctx == nullptr
        || ZL_isError(ZL_ReflectionCtx_setCompressedFrame(
                rctx.get(), frame.data(), frame.size()))) {
        ADD_FAILURE() << "cannot reflect the bf16 frame";
        return {};
    }
    Bf16Streams streams;
    for (size_t i = 0; i < ZL_ReflectionCtx_getNumCodecs_lastChunk(rctx.get());
         ++i) {
        const ZL_CodecInfo* const codec =
                ZL_ReflectionCtx_getCodec_lastChunk(rctx.get(), i);
        if (!ZL_CodecInfo_isStandardCodec(codec)
            || ZL_CodecInfo_getCodecID(codec)
                    != ZL_StandardTransformID_float_deconstruct) {
            continue;
        }
        for (size_t o = 0; o < ZL_CodecInfo_getNumOutputs(codec); ++o) {
            const ZL_DataInfo* const data = ZL_CodecInfo_getOutput(codec, o);
            const auto* const begin =
                    static_cast<const uint8_t*>(ZL_DataInfo_getDataPtr(data));
            const uint8_t* const end = begin + ZL_DataInfo_getContentSize(data);
            // Reflection lists the outputs in encoder order, so classify them
            // by type.
            if (ZL_DataInfo_getType(data) == ZL_Type_struct) {
                streams.signFrac.assign(begin, end);
            } else {
                streams.exponent.assign(begin, end);
            }
        }
    }
    EXPECT_EQ(streams.signFrac.size(), values.size());
    EXPECT_EQ(streams.exponent.size(), values.size());
    return streams;
}

} // namespace openzl::gpu::test
