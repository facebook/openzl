// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/thrift/kernels/decode_thrift_binding.h"

#include <cassert>
#include <span>
#include <stdexcept>
#include <tuple>

#include "custom_transforms/thrift/kernels/decode_thrift_kernel.h"
#include "openzl/common/errors_internal.h"
#include "openzl/decompress/dictx.h"
#include "openzl/shared/varint.h"

namespace {
template <typename Result>
size_t unwrap(Result result)
{
    if (ZL_RES_isError(result)) {
        throw std::runtime_error(ZL_E_codeStr(ZL_RES_error(result)));
    }
    return ZL_RES_value(result);
}

template <size_t idx = 0, typename... Ts>
void fillInputStreams(
        std::tuple<std::span<Ts const>...>& inputs,
        ZL_Input const* src[])
{
    if constexpr (idx < sizeof...(Ts)) {
        using Tuple = std::tuple<std::span<Ts const>...>;
        using Span  = std::tuple_element_t<idx, Tuple>;
        using T     = typename Span::value_type;

        auto& input = std::get<idx>(inputs);
        assert(ZL_Input_type(src[idx]) == ZL_Type_numeric);

        if (sizeof(input[0]) != ZL_Input_eltWidth(src[idx])) {
            throw std::runtime_error("Bad stream width!");
        }

        input = std::span{ (T const*)ZL_Input_ptr(src[idx]),
                           ZL_Input_numElts(src[idx]) };

        fillInputStreams<idx + 1>(inputs, src);
    }
}

template <typename Kernel>
ZL_Report typedTransform(ZL_Decoder* dictx, ZL_Input const* src[]) noexcept
{
    using InputStreams = typename Kernel::InputStreams;
    ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
    ZL_ERR_IF_LT(
            DI_getFrameFormatVersion(dictx),
            9,
            formatVersion_unsupported,
            "Support first added in format version 9");
    try {
        InputStreams inputs;
        fillInputStreams(inputs, src + 1);

        size_t dstCapacity;
        {
            auto const header   = ZL_Decoder_getCodecHeader(dictx);
            uint8_t const* ip   = (uint8_t const*)header.start;
            uint8_t const* iend = ip + header.size;
            dstCapacity         = unwrap(ZL_varintDecode(&ip, iend));
            ZL_ERR_IF_NE(ip, iend, corruption);
        }

        ZL_Output* stream = ZL_Decoder_create1OutStream(dictx, dstCapacity, 1);
        ZL_ERR_IF_NULL(stream, allocation);
        std::span<uint8_t> out = { (uint8_t*)ZL_Output_ptr(stream),
                                   dstCapacity };

        ZL_ERR_IF_NE(ZL_Input_eltWidth(src[0]), 8, corruption);
        std::span<uint64_t const> sizes = {
            (uint64_t const*)ZL_Input_ptr(src[0]), ZL_Input_numElts(src[0])
        };

        for (auto const& size : sizes) {
            auto const written = Kernel{}(dictx, out, inputs, size);
            ZL_ERR_IF_ERR(written);
            assert(ZL_validResult(written) <= out.size());
            out = out.subspan(ZL_validResult(written));
        }

        ZL_ERR_IF(!out.empty(), corruption);

        ZL_ERR_IF_ERR(ZL_Output_commit(stream, dstCapacity));

        return ZL_returnSuccess();
    } catch (std::exception const& e) {
        ZL_ERR(transform_executionFailure,
               "Thrift kernel failure: %s",
               e.what());
    }
}

template <typename Kernel>
ZL_Report registerDTransform(ZL_DCtx* dctx, ZL_IDType transformID)
{
    using InputStreams = typename Kernel::InputStreams;
    std::array<ZL_Type, 1 + std::tuple_size_v<InputStreams>> outStreamTypes;
    for (auto& streamType : outStreamTypes) {
        streamType = ZL_Type_numeric;
    }
    ZL_TypedDecoderDesc desc = {
                .gd = {
                        .CTid = transformID,
                        .inStreamType = ZL_Type_serial,
                        .outStreamTypes = outStreamTypes.data(),
                        .nbOutStreams = outStreamTypes.size(),
                },
                .transform_f = typedTransform<Kernel>,
        };
    return ZL_DCtx_registerTypedDecoder(dctx, &desc);
}
} // namespace

ZL_Report ZS2_ThriftKernel_registerDTransformMapI32Float(
        ZL_DCtx* dctx_,
        ZL_IDType transformID)
{
    struct Transform {
        using InputStreams = std::
                tuple<std::span<uint32_t const>, std::span<uint32_t const>>;

        ZL_Report operator()(
                ZL_Decoder* dictx,
                std::span<uint8_t> out,
                InputStreams& in,
                size_t mapSize)
        {
            ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
            auto& [keys, values] = in;
            ZL_ERR_IF_NE(keys.size(), values.size(), corruption);
            ZL_ERR_IF_LT(keys.size(), mapSize, corruption);
            auto ret = ZS2_ThriftKernel_serializeMapI32Float(
                    out.data(),
                    out.size(),
                    keys.data(),
                    values.data(),
                    mapSize);
            keys   = keys.subspan(mapSize);
            values = values.subspan(mapSize);
            return ret;
        }
    };

    return registerDTransform<Transform>(dctx_, transformID);
}

ZL_Report ZS2_ThriftKernel_registerDTransformMapI32ArrayFloat(
        ZL_DCtx* dctx_,
        ZL_IDType transformID)
{
    struct Transform {
        using InputStreams = std::tuple<
                std::span<uint32_t const>,
                std::span<uint32_t const>,
                std::span<uint32_t const>>;

        ZL_Report operator()(
                ZL_Decoder* dictx,
                std::span<uint8_t> out,
                InputStreams& in,
                size_t mapSize)
        {
            ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
            auto& [keys, lengths, innerValues] = in;
            ZL_ERR_IF_NE(keys.size(), lengths.size(), corruption);
            ZL_ERR_IF_LT(keys.size(), mapSize, corruption);
            auto* innerValuesPtr = innerValues.data();
            auto* innerValuesEnd = innerValues.data() + innerValues.size();
            auto ret             = ZS2_ThriftKernel_serializeMapI32ArrayFloat(
                    out.data(),
                    out.size(),
                    keys.data(),
                    lengths.data(),
                    mapSize,
                    &innerValuesPtr,
                    innerValuesEnd);
            assert(innerValuesPtr <= innerValuesEnd);
            keys        = keys.subspan(mapSize);
            lengths     = lengths.subspan(mapSize);
            innerValues = { innerValuesPtr, innerValuesEnd };
            return ret;
        }
    };

    return registerDTransform<Transform>(dctx_, transformID);
}

ZL_Report ZS2_ThriftKernel_registerDTransformMapI32ArrayI64(
        ZL_DCtx* dctx_,
        ZL_IDType transformID)
{
    struct Transform {
        using InputStreams = std::tuple<
                std::span<uint32_t const>,
                std::span<uint32_t const>,
                std::span<uint64_t const>>;

        ZL_Report operator()(
                ZL_Decoder* dictx,
                std::span<uint8_t> out,
                InputStreams& in,
                size_t mapSize)
        {
            ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
            auto& [keys, lengths, innerValues] = in;
            ZL_ERR_IF_NE(keys.size(), lengths.size(), corruption);
            ZL_ERR_IF_LT(keys.size(), mapSize, corruption);
            auto* innerValuesPtr = innerValues.data();
            auto* innerValuesEnd = innerValues.data() + innerValues.size();
            auto ret             = ZS2_ThriftKernel_serializeMapI32ArrayI64(
                    out.data(),
                    out.size(),
                    keys.data(),
                    lengths.data(),
                    mapSize,
                    &innerValuesPtr,
                    innerValuesEnd);
            assert(innerValuesPtr <= innerValuesEnd);
            keys        = keys.subspan(mapSize);
            lengths     = lengths.subspan(mapSize);
            innerValues = { innerValuesPtr, innerValuesEnd };
            return ret;
        }
    };

    return registerDTransform<Transform>(dctx_, transformID);
}

ZL_Report ZS2_ThriftKernel_registerDTransformMapI32ArrayArrayI64(
        ZL_DCtx* dctx_,
        ZL_IDType transformID)
{
    struct Transform {
        using InputStreams = std::tuple<
                std::span<uint32_t const>,
                std::span<uint32_t const>,
                std::span<uint32_t const>,
                std::span<uint64_t const>>;

        ZL_Report operator()(
                ZL_Decoder* dictx,
                std::span<uint8_t> out,
                InputStreams& in,
                size_t mapSize)
        {
            ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
            auto& [keys, lengths, innerLengths, innerInnerValues] = in;
            ZL_ERR_IF_NE(keys.size(), lengths.size(), corruption);
            ZL_ERR_IF_LT(keys.size(), mapSize, corruption);
            auto* innerLengthsPtr = innerLengths.data();
            auto* innerLengthsEnd = innerLengths.data() + innerLengths.size();
            auto* innerInnerValuesPtr = innerInnerValues.data();
            auto* innerInnerValuesEnd =
                    innerInnerValues.data() + innerInnerValues.size();
            auto ret = ZS2_ThriftKernel_serializeMapI32ArrayArrayI64(
                    out.data(),
                    out.size(),
                    keys.data(),
                    lengths.data(),
                    mapSize,
                    &innerLengthsPtr,
                    innerLengthsEnd,
                    &innerInnerValuesPtr,
                    innerInnerValuesEnd);
            assert(innerLengthsPtr <= innerLengthsEnd);
            assert(innerInnerValuesPtr <= innerInnerValuesEnd);
            keys             = keys.subspan(mapSize);
            lengths          = lengths.subspan(mapSize);
            innerLengths     = { innerLengthsPtr, innerLengthsEnd };
            innerInnerValues = { innerInnerValuesPtr, innerInnerValuesEnd };
            return ret;
        }
    };

    return registerDTransform<Transform>(dctx_, transformID);
}

ZL_Report ZS2_ThriftKernel_registerDTransformMapI32MapI64Float(
        ZL_DCtx* dctx_,
        ZL_IDType transformID)
{
    struct Transform {
        using InputStreams = std::tuple<
                std::span<uint32_t const>,
                std::span<uint32_t const>,
                std::span<uint64_t const>,
                std::span<uint32_t const>>;

        ZL_Report operator()(
                ZL_Decoder* dictx,
                std::span<uint8_t> out,
                InputStreams& in,
                size_t mapSize)
        {
            ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
            auto& [keys, lengths, innerKeys, innerValues] = in;
            ZL_ERR_IF_NE(keys.size(), lengths.size(), corruption);
            ZL_ERR_IF_LT(keys.size(), mapSize, corruption);
            auto* innerKeysPtr   = innerKeys.data();
            auto* innerKeysEnd   = innerKeys.data() + innerKeys.size();
            auto* innerValuesPtr = innerValues.data();
            auto* innerValuesEnd = innerValues.data() + innerValues.size();
            auto ret             = ZS2_ThriftKernel_serializeMapI32MapI64Float(
                    out.data(),
                    out.size(),
                    keys.data(),
                    lengths.data(),
                    mapSize,
                    &innerKeysPtr,
                    innerKeysEnd,
                    &innerValuesPtr,
                    innerValuesEnd);
            assert(innerKeysPtr <= innerKeysEnd);
            assert(innerValuesPtr <= innerValuesEnd);
            keys        = keys.subspan(mapSize);
            lengths     = lengths.subspan(mapSize);
            innerKeys   = { innerKeysPtr, innerKeysEnd };
            innerValues = { innerValuesPtr, innerValuesEnd };
            return ret;
        }
    };

    return registerDTransform<Transform>(dctx_, transformID);
}

ZL_Report ZS2_ThriftKernel_registerDTransformArrayI64(
        ZL_DCtx* dctx_,
        ZL_IDType transformID)
{
    struct Transform {
        using InputStreams = std::tuple<std::span<uint64_t const>>;

        ZL_Report operator()(
                ZL_Decoder* dictx,
                std::span<uint8_t> out,
                InputStreams& in,
                size_t arraySize)
        {
            ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
            auto& [values] = in;
            ZL_ERR_IF_LT(values.size(), arraySize, corruption);
            auto ret = ZS2_ThriftKernel_serializeArrayI64(
                    out.data(), out.size(), values.data(), arraySize);
            values = values.subspan(arraySize);
            return ret;
        }
    };

    return registerDTransform<Transform>(dctx_, transformID);
}

ZL_Report ZS2_ThriftKernel_registerDTransformArrayI32(
        ZL_DCtx* dctx_,
        ZL_IDType transformID)
{
    struct Transform {
        using InputStreams = std::tuple<std::span<uint32_t const>>;

        ZL_Report operator()(
                ZL_Decoder* dictx,
                std::span<uint8_t> out,
                InputStreams& in,
                size_t arraySize)
        {
            ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
            auto& [values] = in;
            ZL_ERR_IF_LT(values.size(), arraySize, corruption);
            auto ret = ZS2_ThriftKernel_serializeArrayI32(
                    out.data(), out.size(), values.data(), arraySize);
            values = values.subspan(arraySize);
            return ret;
        }
    };

    return registerDTransform<Transform>(dctx_, transformID);
}

ZL_Report ZS2_ThriftKernel_registerDTransformArrayFloat(
        ZL_DCtx* dctx_,
        ZL_IDType transformID)
{
    struct Transform {
        using InputStreams = std::tuple<std::span<uint32_t const>>;

        ZL_Report operator()(
                ZL_Decoder* dictx,
                std::span<uint8_t> out,
                InputStreams& in,
                size_t arraySize)
        {
            ZL_RESULT_DECLARE_SCOPE_REPORT(dictx);
            auto& [values] = in;
            ZL_ERR_IF_LT(values.size(), arraySize, corruption);
            auto ret = ZS2_ThriftKernel_serializeArrayFloat(
                    out.data(), out.size(), values.data(), arraySize);
            values = values.subspan(arraySize);
            return ret;
        }
    };

    return registerDTransform<Transform>(dctx_, transformID);
}
