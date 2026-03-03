// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/errors_internal.h"
#include "openzl/zl_data.h"
#include "openzl/zl_dtransform.h"

#include <cstring>

#include <folly/Conv.h>

namespace zstrong {
namespace {

size_t constexpr maxStrLen(double)
{
    return 26;
}

class StreamAppender {
   public:
    explicit StreamAppender(ZL_Output* stream)
            : buffer_((char*)ZL_Output_ptr(stream))
    {
    }

    void append(char const* ptr, size_t length)
    {
        std::memcpy(buffer_ + idx_, ptr, length);
        idx_ += length;
    }

    void push_back(char c)
    {
        buffer_[idx_++] = c;
    }

    size_t commitField()
    {
        size_t const fieldSize = idx_ - prev_;
        prev_                  = idx_;
        return fieldSize;
    }

   private:
    char* buffer_;
    size_t idx_{ 0 };
    size_t prev_{ 0 };
};
} // namespace
} // namespace zstrong

namespace folly {
template <>
struct IsSomeString<zstrong::StreamAppender> : std::true_type {};
} // namespace folly

namespace zstrong {
namespace {
template <typename T>
ZL_Report parseDecode(ZL_Decoder* dictx, ZL_Input const* inputs[]) noexcept
{
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_Input const* numbers          = inputs[0];
    ZL_Input const* exceptionIndices = inputs[1];
    ZL_Input const* exceptions       = inputs[2];

    ZL_ERR_IF_NE(
            ZL_Input_numElts(exceptionIndices),
            ZL_Input_numElts(exceptions),
            corruption);
    ZL_ERR_IF_NE(ZL_Input_eltWidth(exceptionIndices), 4, corruption);
    ZL_ERR_IF_NE(ZL_Input_eltWidth(numbers), sizeof(T), corruption);

    size_t const outBound = ZL_Input_contentSize(exceptions)
            + ZL_Input_numElts(numbers) * maxStrLen(T{});
    ZL_Output* outStream = ZL_Decoder_create1OutStream(dictx, outBound, 1);
    ZL_ERR_IF_NULL(outStream, allocation);

    StreamAppender outAppender{ outStream };

    size_t const nbElts =
            ZL_Input_numElts(numbers) + ZL_Input_numElts(exceptions);

    uint32_t* fieldSizes = ZL_Output_reserveStringLens(outStream, nbElts);
    ZL_ERR_IF_NULL(fieldSizes, allocation);

    auto nums          = (T const*)ZL_Input_ptr(numbers);
    auto const numsEnd = nums + ZL_Input_numElts(numbers);

    auto exIdxs          = (uint32_t const*)ZL_Input_ptr(exceptionIndices);
    auto const exIdxsEnd = exIdxs + ZL_Input_numElts(exceptionIndices);

    auto exData  = (char const*)ZL_Input_ptr(exceptions);
    auto exSizes = ZL_Input_stringLens(exceptions);

    for (size_t i = 0; i < nbElts; ++i) {
        if (exIdxs < exIdxsEnd && i == *exIdxs) {
            ++exIdxs;
            auto const exSize = *exSizes++;
            outAppender.append(exData, exSize);
            exData += exSize;
        } else {
            ZL_ERR_IF_EQ(nums, numsEnd, srcSize_tooSmall);
            folly::toAppend(*nums++, &outAppender);
        }
        fieldSizes[i] = outAppender.commitField();
    }

    ZL_ERR_IF_NE(nums, numsEnd, corruption);
    ZL_ERR_IF_NE(exIdxs, exIdxsEnd, corruption);

    ZL_ERR_IF_ERR(ZL_Output_commit(outStream, nbElts));

    return ZL_returnSuccess();
}

} // namespace
} // namespace zstrong

extern "C" ZL_Report ZS2_DCtx_registerParseFloat64(
        ZL_DCtx* dctx,
        ZL_IDType transformID)
{
    std::array<ZL_Type, 3> const kOutStreams = {
        ZL_Type_numeric,
        ZL_Type_numeric,
        ZL_Type_string,
    };
    ZL_TypedGraphDesc graph = {
        .CTid           = transformID,
        .inStreamType   = ZL_Type_string,
        .outStreamTypes = kOutStreams.data(),
        .nbOutStreams   = kOutStreams.size(),
    };
    ZL_TypedDecoderDesc desc = {
        .gd          = graph,
        .transform_f = zstrong::parseDecode<double>,
        .name        = "parse float64",
    };
    return ZL_DCtx_registerTypedDecoder(dctx, &desc);
}
