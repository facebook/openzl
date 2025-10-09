// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_transforms/thrift/kernels/encode_thrift_binding.h"

#include <folly/memory/UninitializedMemoryHacks.h>

#include "custom_transforms/thrift/kernels/encode_thrift_kernel.h"
#include "custom_transforms/thrift/kernels/thrift_kernel_utils.h"
#include "openzl/common/errors_internal.h"
#include "openzl/shared/varint.h"

namespace {
using namespace zstrong::thrift;

template <typename T>
ZL_Report commitOutputStream(
        ZL_Encoder* eictx,
        size_t idx,
        std::vector<std::vector<T> const*> const& vectors)
{
    size_t size = 0;
    for (auto const* vector : vectors) {
        size += vector->size();
    }

    ZL_Output* stream =
            ZL_Encoder_createTypedStream(eictx, idx, size, sizeof(T));
    ZL_RET_R_IF_NULL(allocation, stream);
    uint8_t* op = (uint8_t*)ZL_Output_ptr(stream);
    for (auto const* vector : vectors) {
        if (vector->size() > 0)
            memcpy(op, vector->data(), vector->size() * sizeof(T));
        op += vector->size() * sizeof(T);
    }
    assert((op - (uint8_t*)ZL_Output_ptr(stream)) == size * sizeof(T));
    ZL_RET_R_IF_ERR(ZL_Output_commit(stream, size));
    return ZL_returnSuccess();
}

template <typename T>
ZL_Report commitOutputStream(
        ZL_Encoder* eictx,
        size_t idx,
        std::vector<ZeroCopyDynamicOutput<T> const*> const& outs)
{
    size_t size = 0;
    for (auto const* out : outs) {
        size += out->size();
    }
    ZL_Output* stream =
            ZL_Encoder_createTypedStream(eictx, idx, size, sizeof(T));
    ZL_RET_R_IF_NULL(allocation, stream);
    uint8_t* op = (uint8_t*)ZL_Output_ptr(stream);
    for (auto const* out : outs) {
        out->copyToBuffer(op, out->nbytes());
        op += out->nbytes();
    }
    assert((op - (uint8_t*)ZL_Output_ptr(stream)) == size * sizeof(T));
    ZL_RET_R_IF_ERR(ZL_Output_commit(stream, size));
    return ZL_returnSuccess();
}

template <size_t idx = 0, typename... Ts>
ZL_Report commitOutputStreams(
        ZL_Encoder* eictx,
        std::vector<std::tuple<Ts...>> const& outputs)
{
    if constexpr (idx < sizeof...(Ts)) {
        std::vector<std::tuple_element_t<idx, std::tuple<Ts...>> const*> output;
        output.reserve(outputs.size());
        for (auto&& tuple : outputs) {
            output.push_back(&std::get<idx>(tuple));
        }
        ZL_RET_R_IF_ERR(commitOutputStream(eictx, idx + 1, output));
        return commitOutputStreams<idx + 1>(eictx, outputs);
    } else {
        return ZL_returnSuccess();
    }
}

/// Kernel takes an input and produces a tuple of outputs.
/// The first output must have the .size() of the container.
template <typename Kernel>
ZL_Report typedTransform(ZL_Encoder* eictx, ZL_Input const* input) noexcept
{
    // These transforms were added in version 9
    ZL_RET_R_IF_LT(
            formatVersion_unsupported,
            ZL_Encoder_getCParam(eictx, ZL_CParam_formatVersion),
            9);

    assert(ZL_Input_type(input) == ZL_Type_serial);
    try {
        using KernelReturnType =
                std::invoke_result_t<Kernel, std::string_view&>;
        std::vector<KernelReturnType> outs;
        std::vector<uint64_t> lengths;
        std::string_view data(
                (char const*)ZL_Input_ptr(input), ZL_Input_numElts(input));
        while (!data.empty()) {
            auto out = Kernel{}(data);
            lengths.push_back(std::get<0>(out).size());
            outs.push_back(std::move(out));
        }

        // Send the original source size as a header
        {
            uint64_t const serializedSize = ZL_Input_numElts(input);
            uint8_t header[ZL_VARINT_LENGTH_64];
            size_t const headerSize = ZL_varintEncode(serializedSize, header);
            ZL_Encoder_sendCodecHeader(eictx, header, headerSize);
        }

        // Add the lengths stream
        {
            std::vector<std::vector<uint64_t> const*> outs_2 = { &lengths };
            ZL_RET_R_IF_ERR(commitOutputStream(eictx, 0, outs_2));
        }

        ZL_RET_R_IF_ERR(commitOutputStreams(eictx, outs));

        return ZL_returnSuccess();
    } catch (std::exception const& e) {
        ZL_RET_R_ERR(
                transform_executionFailure,
                "Thrift kernel failure: %s",
                e.what());
    }
}

template <typename Kernel>
ZL_NodeID registerCTransform(ZL_Compressor* cgraph, ZL_IDType transformID)
{
    using KernelReturnType = std::invoke_result_t<Kernel, std::string_view&>;
    std::array<ZL_Type, 1 + std::tuple_size_v<KernelReturnType>> outStreamTypes;
    for (auto& streamType : outStreamTypes) {
        streamType = ZL_Type_numeric;
    }
    ZL_TypedEncoderDesc desc = {
                .gd = {
                        .CTid = transformID,
                        .inStreamType = ZL_Type_serial,
                        .outStreamTypes = outStreamTypes.data(),
                        .nbOutStreams = outStreamTypes.size(),
                },
                .transform_f = typedTransform<Kernel>,
        };
    return ZL_Compressor_registerTypedEncoder(cgraph, &desc);
}

size_t unwrap(ZL_Report report)
{
    if (ZL_isError(report)) {
        throw std::runtime_error(ZL_E_codeStr(ZL_RES_error(report)));
    }
    return ZL_validResult(report);
}
} // namespace

/// Input: Thrift Compact map<i32, float>
/// Output 1: numeric i32 keys
/// Output 2: numeric floats
ZL_NodeID ZL_ThriftKernel_registerCTransformMapI32Float(
        ZL_Compressor* cgraph,
        ZL_IDType transformID)
{
    struct Transform {
        std::tuple<std::vector<uint32_t>, std::vector<uint32_t>> operator()(
                std::string_view& src)
        {
            auto const mapSize =
                    unwrap(ZL_ThriftKernel_getMapSize(src.data(), src.size()));
            std::vector<uint32_t> keys, values;
            folly::resizeWithoutInitialization(keys, mapSize);
            folly::resizeWithoutInitialization(values, mapSize);
            size_t const srcConsumed =
                    unwrap(ZL_ThriftKernel_deserializeMapI32Float(
                            keys.data(),
                            values.data(),
                            src.data(),
                            src.size(),
                            mapSize));
            src.remove_prefix(srcConsumed);
            return std::tuple{ std::move(keys), std::move(values) };
        }
    };

    return registerCTransform<Transform>(cgraph, transformID);
}

/// Input: Thrift Compact map<i32, list<float>>
/// Output 1: numeric i32 keys
/// Output 2: numeric u32 lengths
/// Output 3: numeric floats
ZL_NodeID ZL_ThriftKernel_registerCTransformMapI32ArrayFloat(
        ZL_Compressor* cgraph,
        ZL_IDType transformID)
{
    struct Transform {
        std::tuple<
                std::vector<uint32_t>,
                std::vector<uint32_t>,
                ZeroCopyDynamicOutput<uint32_t>>
        operator()(std::string_view& src)
        {
            auto const mapSize =
                    unwrap(ZL_ThriftKernel_getMapSize(src.data(), src.size()));
            std::vector<uint32_t> keys, lengths;
            folly::resizeWithoutInitialization(keys, mapSize);
            folly::resizeWithoutInitialization(lengths, mapSize);
            ZeroCopyDynamicOutput<uint32_t> innerValues;
            size_t const srcConsumed =
                    unwrap(ZL_ThriftKernel_deserializeMapI32ArrayFloat(
                            keys.data(),
                            lengths.data(),
                            innerValues.asCType(),
                            src.data(),
                            src.size(),
                            mapSize));
            src.remove_prefix(srcConsumed);
            return std::tuple{ std::move(keys),
                               std::move(lengths),
                               std::move(innerValues) };
        }
    };

    return registerCTransform<Transform>(cgraph, transformID);
}

/// Input: Thrift Compact map<i32, list<i64>>
/// Output 1: numeric i32 keys
/// Output 2: numeric u32 lengths
/// Output 3: numeric i64
ZL_NodeID ZL_ThriftKernel_registerCTransformMapI32ArrayI64(
        ZL_Compressor* cgraph,
        ZL_IDType transformID)
{
    struct Transform {
        std::tuple<
                std::vector<uint32_t>,
                std::vector<uint32_t>,
                ZeroCopyDynamicOutput<uint64_t>>
        operator()(std::string_view& src)
        {
            auto const mapSize =
                    unwrap(ZL_ThriftKernel_getMapSize(src.data(), src.size()));
            std::vector<uint32_t> keys, lengths;
            folly::resizeWithoutInitialization(keys, mapSize);
            folly::resizeWithoutInitialization(lengths, mapSize);
            ZeroCopyDynamicOutput<uint64_t> innerValues;
            size_t const srcConsumed =
                    unwrap(ZL_ThriftKernel_deserializeMapI32ArrayI64(
                            keys.data(),
                            lengths.data(),
                            innerValues.asCType(),
                            src.data(),
                            src.size(),
                            mapSize));
            src.remove_prefix(srcConsumed);
            return std::tuple{ std::move(keys),
                               std::move(lengths),
                               std::move(innerValues) };
        }
    };

    return registerCTransform<Transform>(cgraph, transformID);
}

/// Input: Thrift Compact map<i32, list<list<i64>>>
/// Output 1: numeric i32 keys
/// Output 2: numeric u32 outter list lengths
/// Output 3: numeric u32 inner list lengths
/// Output 4: numeric i64
ZL_NodeID ZL_ThriftKernel_registerCTransformMapI32ArrayArrayI64(
        ZL_Compressor* cgraph,
        ZL_IDType transformID)
{
    struct Transform {
        std::tuple<
                std::vector<uint32_t>,
                std::vector<uint32_t>,
                ZeroCopyDynamicOutput<uint32_t>,
                ZeroCopyDynamicOutput<uint64_t>>
        operator()(std::string_view& src)
        {
            auto const mapSize =
                    unwrap(ZL_ThriftKernel_getMapSize(src.data(), src.size()));
            std::vector<uint32_t> keys, lengths;
            folly::resizeWithoutInitialization(keys, mapSize);
            folly::resizeWithoutInitialization(lengths, mapSize);
            ZeroCopyDynamicOutput<uint32_t> innerLengths;
            ZeroCopyDynamicOutput<uint64_t> innerInnerValues;
            size_t const srcConsumed =
                    unwrap(ZL_ThriftKernel_deserializeMapI32ArrayArrayI64(
                            keys.data(),
                            lengths.data(),
                            innerLengths.asCType(),
                            innerInnerValues.asCType(),
                            src.data(),
                            src.size(),
                            mapSize));
            src.remove_prefix(srcConsumed);
            return std::tuple{ std::move(keys),
                               std::move(lengths),
                               std::move(innerLengths),
                               std::move(innerInnerValues) };
        }
    };

    return registerCTransform<Transform>(cgraph, transformID);
}

/// Input: Thrift Compact map<i32, map<i64, float>>
/// Output 1: numeric i32 keys
/// Output 2: numeric u32 lengths
/// Output 3: numeric i64 keys
/// Output 4: numeric float values
ZL_NodeID ZL_ThriftKernel_registerCTransformMapI32MapI64Float(
        ZL_Compressor* cgraph,
        ZL_IDType transformID)
{
    struct Transform {
        std::tuple<
                std::vector<uint32_t>,
                std::vector<uint32_t>,
                ZeroCopyDynamicOutput<uint64_t>,
                ZeroCopyDynamicOutput<uint32_t>>
        operator()(std::string_view& src)
        {
            auto const mapSize =
                    unwrap(ZL_ThriftKernel_getMapSize(src.data(), src.size()));
            std::vector<uint32_t> keys, lengths;
            folly::resizeWithoutInitialization(keys, mapSize);
            folly::resizeWithoutInitialization(lengths, mapSize);
            ZeroCopyDynamicOutput<uint64_t> innerKeys;
            ZeroCopyDynamicOutput<uint32_t> innerValues;
            size_t const srcConsumed =
                    unwrap(ZL_ThriftKernel_deserializeMapI32MapI64Float(
                            keys.data(),
                            lengths.data(),
                            innerKeys.asCType(),
                            innerValues.asCType(),
                            src.data(),
                            src.size(),
                            mapSize));
            src.remove_prefix(srcConsumed);
            return std::tuple{ std::move(keys),
                               std::move(lengths),
                               std::move(innerKeys),
                               std::move(innerValues) };
        }
    };

    return registerCTransform<Transform>(cgraph, transformID);
}

/// Input: Thrift Compact list<i64>
/// Output 1: numeric i64
ZL_NodeID ZL_ThriftKernel_registerCTransformArrayI64(
        ZL_Compressor* cgraph,
        ZL_IDType transformID)
{
    struct Transform {
        std::tuple<std::vector<uint64_t>> operator()(std::string_view& src)
        {
            auto const arraySize = unwrap(
                    ZL_ThriftKernel_getArraySize(src.data(), src.size()));
            std::vector<uint64_t> values;
            folly::resizeWithoutInitialization(values, arraySize);
            size_t const srcConsumed =
                    unwrap(ZL_ThriftKernel_deserializeArrayI64(
                            values.data(), src.data(), src.size(), arraySize));
            src.remove_prefix(srcConsumed);
            return std::tuple{ std::move(values) };
        }
    };

    return registerCTransform<Transform>(cgraph, transformID);
}

/// Input: Thrift Compact list<i32>
/// Output 1: numeric i32
ZL_NodeID ZL_ThriftKernel_registerCTransformArrayI32(
        ZL_Compressor* cgraph,
        ZL_IDType transformID)
{
    struct Transform {
        std::tuple<std::vector<uint32_t>> operator()(std::string_view& src)
        {
            auto const arraySize = unwrap(
                    ZL_ThriftKernel_getArraySize(src.data(), src.size()));
            std::vector<uint32_t> values;
            folly::resizeWithoutInitialization(values, arraySize);
            size_t const srcConsumed =
                    unwrap(ZL_ThriftKernel_deserializeArrayI32(
                            values.data(), src.data(), src.size(), arraySize));
            src.remove_prefix(srcConsumed);
            return std::tuple{ std::move(values) };
        }
    };

    return registerCTransform<Transform>(cgraph, transformID);
}

/// Input: Thrift Compact list<float>
/// Output 1: numeric float
ZL_NodeID ZL_ThriftKernel_registerCTransformArrayFloat(
        ZL_Compressor* cgraph,
        ZL_IDType transformID)
{
    struct Transform {
        std::tuple<std::vector<uint32_t>> operator()(std::string_view& src)
        {
            auto const arraySize = unwrap(
                    ZL_ThriftKernel_getArraySize(src.data(), src.size()));
            std::vector<uint32_t> values;
            folly::resizeWithoutInitialization(values, arraySize);
            size_t const srcConsumed =
                    unwrap(ZL_ThriftKernel_deserializeArrayFloat(
                            values.data(), src.data(), src.size(), arraySize));
            src.remove_prefix(srcConsumed);
            return std::tuple{ std::move(values) };
        }
    };

    return registerCTransform<Transform>(cgraph, transformID);
}
