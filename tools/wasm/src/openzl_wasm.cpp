// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <memory>

#include "cli/utils/profile_graphs.h"
#include "tools/wasm/src/openzl_wasm.h"

#include "openzl/shared/mem.h" // ZL_memcpy
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_compressor_serialization.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_errors.h"
#include "openzl/zl_segmenter.h"
#include "openzl/zl_version.h"

#if defined(__EMSCRIPTEN__)
#    include <emscripten/emscripten.h>
#else
#    define EMSCRIPTEN_KEEPALIVE
#endif

namespace {

// Owns each OpenZL handle and invokes its matching free function on every exit.
template <typename T, void (*Free)(T*)>
struct Deleter {
    void operator()(T* p) const noexcept
    {
        Free(p);
    }
};

using CompressorPtr = std::
        unique_ptr<ZL_Compressor, Deleter<ZL_Compressor, ZL_Compressor_free>>;
using SerializerPtr = std::unique_ptr<
        ZL_CompressorSerializer,
        Deleter<ZL_CompressorSerializer, ZL_CompressorSerializer_free>>;
using DeserializerPtr = std::unique_ptr<
        ZL_CompressorDeserializer,
        Deleter<ZL_CompressorDeserializer, ZL_CompressorDeserializer_free>>;
using CCtxPtr = std::unique_ptr<ZL_CCtx, Deleter<ZL_CCtx, ZL_CCtx_free>>;
using DCtxPtr = std::unique_ptr<ZL_DCtx, Deleter<ZL_DCtx, ZL_DCtx_free>>;

// Output buffers remain locally owned until success transfers ownership to JS.
// JS releases transferred buffers with openzl_wasm_free().
struct FreeDeleter {
    void operator()(void* p) const noexcept
    {
        free(p);
    }
};
using BufferPtr = std::unique_ptr<uint8_t, FreeDeleter>;

BufferPtr allocBuffer(size_t size)
{
    // A zero-size output still gets an allocation, so success always yields a
    // non-NULL pointer and the caller never has to special-case empty.
    return BufferPtr{ static_cast<uint8_t*>(malloc(size ? size : 1)) };
}

struct Profile {
    const char* name;
    size_t eltByteWidth;
    bool isSigned;
};

// Indexed by openzl_wasm_Profile ordering must match
constexpr Profile kProfiles[OPENZL_WASM_PROFILE_COUNT] = {
    { "serial", 0, false }, { "u8", 1, false },  { "i8", 1, true },
    { "u16", 2, false },    { "i16", 2, true },  { "u32", 4, false },
    { "i32", 4, true },     { "u64", 8, false }, { "i64", 8, true },
};

ZL_ErrorCode buildProfileCompressor(
        openzl_wasm_Profile profile,
        CompressorPtr& out)
{
    out.reset();

    if (profile < 0 || profile >= OPENZL_WASM_PROFILE_COUNT) {
        return ZL_ErrorCode_invalidName;
    }
    const Profile& profileDef = kProfiles[profile];

    CompressorPtr comp{ ZL_Compressor_create() };
    if (!comp) {
        return ZL_ErrorCode_allocation;
    }

    // Format version is mandatory, use latest supported for WASM.
    ZL_Report r = ZL_Compressor_setParameter(
            comp.get(), ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION);
    if (ZL_isError(r)) {
        return ZL_errorCode(r);
    }

    ZL_GraphID graph = profile == OPENZL_WASM_PROFILE_SERIAL
            ? openzl::profiles::buildSerialGraph(
                      comp.get(), ZL_DEFAULT_SEGMENTER_CHUNK_BYTE_SIZE)
            : openzl::profiles::buildIntGraph(
                      comp.get(),
                      profileDef.eltByteWidth,
                      profileDef.isSigned,
                      ZL_DEFAULT_SEGMENTER_CHUNK_BYTE_SIZE);
    if (!ZL_GraphID_isValid(graph)) {
        return ZL_ErrorCode_graph_invalid;
    }

    ZL_Report sel = ZL_Compressor_selectStartingGraphID(comp.get(), graph);
    if (ZL_isError(sel)) {
        return ZL_errorCode(sel);
    }

    out = std::move(comp);
    return ZL_ErrorCode_no_error;
}

ZL_ErrorCode deserializeCompressor(
        const uint8_t* serialized,
        size_t serializedSize,
        CompressorPtr& out)
{
    out.reset();

    CompressorPtr comp{ ZL_Compressor_create() };
    if (!comp) {
        return ZL_ErrorCode_allocation;
    }
    DeserializerPtr deser{ ZL_CompressorDeserializer_create() };
    if (!deser) {
        return ZL_ErrorCode_allocation;
    }

    // TODO: Process dependencies for other profiles. A serial or integer
    // profile graph has no unmet dependencies

    ZL_Report r = ZL_CompressorDeserializer_deserialize(
            deser.get(), comp.get(), serialized, serializedSize, nullptr, 0);
    if (ZL_isError(r)) {
        return ZL_errorCode(r);
    }

    out = std::move(comp);
    return ZL_ErrorCode_no_error;
}

ZL_ErrorCode tryCompress(
        ZL_Compressor* comp,
        const uint8_t* src,
        size_t srcSize,
        uint8_t* dst,
        size_t dstCapacity,
        size_t* written)
{
    CCtxPtr cctx{ ZL_CCtx_create() };
    if (!cctx) {
        return ZL_ErrorCode_allocation;
    }
    ZL_Report ref = ZL_CCtx_refCompressor(cctx.get(), comp);
    if (ZL_isError(ref)) {
        return ZL_errorCode(ref);
    }
    ZL_Report cr = ZL_CCtx_compress(cctx.get(), dst, dstCapacity, src, srcSize);
    if (ZL_isError(cr)) {
        return ZL_errorCode(cr);
    }
    *written = ZL_validResult(cr);
    return ZL_ErrorCode_no_error;
}

double nowMs()
{
#if defined(__EMSCRIPTEN__)
    return emscripten_get_now();
#else
    // Native build, so the gtest can run without Emscripten. Use
    // std::chrono::steady_clock for portability (Windows doesn't have
    // CLOCK_MONOTONIC / clock_gettime).
    auto now = std::chrono::steady_clock::now().time_since_epoch();
    return std::chrono::duration<double, std::milli>(now).count();
#endif
}

} // namespace

EMSCRIPTEN_KEEPALIVE
void* openzl_wasm_malloc(size_t size)
{
    return malloc(size);
}

EMSCRIPTEN_KEEPALIVE
void openzl_wasm_free(void* buf)
{
    free(buf);
}

EMSCRIPTEN_KEEPALIVE
const char* openzl_wasm_errorString(ZL_ErrorCode code)
{
    return ZL_ErrorCode_toString(code);
}

EMSCRIPTEN_KEEPALIVE
int openzl_wasm_maxBenchmarkIterations(void)
{
    return OPENZL_WASM_BENCHMARK_MAX_ITERATIONS;
}

EMSCRIPTEN_KEEPALIVE
const char* openzl_wasm_profileName(openzl_wasm_Profile profile)
{
    if (profile < 0 || profile >= OPENZL_WASM_PROFILE_COUNT) {
        return nullptr;
    }
    return kProfiles[profile].name;
}

EMSCRIPTEN_KEEPALIVE
ZL_ErrorCode openzl_wasm_getSerializedCompressor(
        openzl_wasm_Profile profile,
        uint8_t** outBuf,
        size_t* outSize)
{
    if (!outBuf || !outSize) {
        return ZL_ErrorCode_parameter_invalid;
    }
    *outBuf  = nullptr;
    *outSize = 0;

    CompressorPtr comp;
    ZL_ErrorCode code = buildProfileCompressor(profile, comp);
    if (code != ZL_ErrorCode_no_error) {
        return code;
    }

    SerializerPtr ser{ ZL_CompressorSerializer_create() };
    if (!ser) {
        return ZL_ErrorCode_allocation;
    }

    // Passing NULL/0 asks the serializer to size the output and allocate it
    void* out    = nullptr;
    size_t outSz = 0;
    ZL_Report sr = ZL_CompressorSerializer_serialize(
            ser.get(), comp.get(), &out, &outSz);
    if (ZL_isError(sr)) {
        return ZL_errorCode(sr);
    }

    // Copy the serializer's bytes into memory the caller owns, before
    // freeing `ser` takes them away.
    BufferPtr buf = allocBuffer(outSz);
    if (!buf) {
        return ZL_ErrorCode_allocation;
    }
    ZL_memcpy(buf.get(), out, outSz);

    *outBuf  = buf.release();
    *outSize = outSz;
    return ZL_ErrorCode_no_error;
}

EMSCRIPTEN_KEEPALIVE
ZL_ErrorCode openzl_wasm_compress(
        const uint8_t* compressor,
        size_t compressorSize,
        const uint8_t* src,
        size_t srcSize,
        uint8_t** outBuf,
        size_t* outSize)
{
    if (!outBuf || !outSize) {
        return ZL_ErrorCode_parameter_invalid;
    }
    *outBuf  = nullptr;
    *outSize = 0;
    if (!compressor || compressorSize == 0) {
        return ZL_ErrorCode_parameter_invalid;
    }
    // src may be NULL when srcSize is 0, so an empty input costs no
    // allocation on the JS side.
    if (srcSize != 0 && !src) {
        return ZL_ErrorCode_parameter_invalid;
    }

    CompressorPtr comp;
    ZL_ErrorCode code = deserializeCompressor(compressor, compressorSize, comp);
    if (code != ZL_ErrorCode_no_error) {
        return code;
    }

    const size_t capacity = ZL_compressBound(srcSize);
    BufferPtr buf         = allocBuffer(capacity);
    if (!buf) {
        return ZL_ErrorCode_allocation;
    }

    size_t written = 0;
    code = tryCompress(comp.get(), src, srcSize, buf.get(), capacity, &written);
    if (code != ZL_ErrorCode_no_error) {
        return code;
    }

    *outBuf  = buf.release();
    *outSize = written;
    return ZL_ErrorCode_no_error;
}

EMSCRIPTEN_KEEPALIVE
ZL_ErrorCode openzl_wasm_getDecompressedSize(
        const uint8_t* src,
        size_t srcSize,
        size_t* outSize)
{
    if (!outSize) {
        return ZL_ErrorCode_parameter_invalid;
    }
    *outSize = 0;
    if (!src) {
        return ZL_ErrorCode_parameter_invalid;
    }
    ZL_Report r = ZL_getDecompressedSize(src, srcSize);
    if (ZL_isError(r)) {
        return ZL_errorCode(r);
    }
    *outSize = ZL_validResult(r);
    return ZL_ErrorCode_no_error;
}

EMSCRIPTEN_KEEPALIVE
ZL_ErrorCode openzl_wasm_decompress(
        const uint8_t* src,
        size_t srcSize,
        uint8_t** outBuf,
        size_t* outSize)
{
    if (!outBuf || !outSize) {
        return ZL_ErrorCode_parameter_invalid;
    }
    *outBuf  = nullptr;
    *outSize = 0;
    if (!src) {
        return ZL_ErrorCode_parameter_invalid;
    }

    // The frame header carries the exact size
    size_t capacity = 0;
    ZL_ErrorCode code =
            openzl_wasm_getDecompressedSize(src, srcSize, &capacity);
    if (code != ZL_ErrorCode_no_error) {
        return code;
    }

    BufferPtr buf = allocBuffer(capacity);
    if (!buf) {
        return ZL_ErrorCode_allocation;
    }
    DCtxPtr dctx{ ZL_DCtx_create() };
    if (!dctx) {
        return ZL_ErrorCode_allocation;
    }
    ZL_Report r =
            ZL_DCtx_decompress(dctx.get(), buf.get(), capacity, src, srcSize);
    if (ZL_isError(r)) {
        return ZL_errorCode(r);
    }

    *outBuf  = buf.release();
    *outSize = ZL_validResult(r);
    return ZL_ErrorCode_no_error;
}

EMSCRIPTEN_KEEPALIVE
ZL_ErrorCode openzl_wasm_benchmarkCompress(
        const uint8_t* compressor,
        size_t compressorSize,
        const uint8_t* src,
        size_t srcSize,
        size_t iterations,
        uint8_t** outBuf,
        size_t* outSize,
        double* outMs)
{
    if (!outBuf || !outSize || !outMs) {
        return ZL_ErrorCode_parameter_invalid;
    }
    *outBuf  = nullptr;
    *outSize = 0;
    *outMs   = 0;
    if (!compressor || compressorSize == 0) {
        return ZL_ErrorCode_parameter_invalid;
    }
    if (srcSize != 0 && !src) {
        return ZL_ErrorCode_parameter_invalid;
    }
    // Rejected out of bound iterations, js side clamps it so this should not be
    // triggered
    if (iterations < 1 || iterations > OPENZL_WASM_BENCHMARK_MAX_ITERATIONS) {
        return ZL_ErrorCode_parameter_invalid;
    }

    CompressorPtr comp;
    ZL_ErrorCode code = deserializeCompressor(compressor, compressorSize, comp);
    if (code != ZL_ErrorCode_no_error) {
        return code;
    }

    // Deserialized once and reffed once, so the loop times compression
    // alone.
    CCtxPtr cctx{ ZL_CCtx_create() };
    if (!cctx) {
        return ZL_ErrorCode_allocation;
    }

    // Everything (including compressors) gets reset between compression
    // sessions by default, need to include sticky flag otherwise second
    // iteration of benchmark would be measuring something completely different.
    ZL_Report sticky =
            ZL_CCtx_setParameter(cctx.get(), ZL_CParam_stickyParameters, 1);
    if (ZL_isError(sticky)) {
        return ZL_errorCode(sticky);
    }
    ZL_Report ref = ZL_CCtx_refCompressor(cctx.get(), comp.get());
    if (ZL_isError(ref)) {
        return ZL_errorCode(ref);
    }

    const size_t capacity = ZL_compressBound(srcSize);
    BufferPtr buf         = allocBuffer(capacity);
    if (!buf) {
        return ZL_ErrorCode_allocation;
    }

    // A run before the clock starts gives the size to compare against, and
    // warms whatever the first call would otherwise pay for.
    ZL_Report first =
            ZL_CCtx_compress(cctx.get(), buf.get(), capacity, src, srcSize);
    if (ZL_isError(first)) {
        return ZL_errorCode(first);
    }
    const size_t compressedSize = ZL_validResult(first);

    size_t lastSize    = 0;
    const double start = nowMs();
    for (size_t i = 0; i < iterations; i++) {
        ZL_Report r =
                ZL_CCtx_compress(cctx.get(), buf.get(), capacity, src, srcSize);
        if (ZL_isError(r)) {
            code = ZL_errorCode(r);
            break;
        }
        lastSize = ZL_validResult(r);
    }
    const double elapsed = nowMs() - start;

    if (code != ZL_ErrorCode_no_error) {
        return code;
    }
    // Compared after the clock stops, so the check costs nothing measured.
    if (lastSize != compressedSize) {
        return ZL_ErrorCode_GENERIC;
    }

    // `buf` holds the last iteration's frame, which the check above proved
    // identical in size to the first, ownership moves to the caller.
    *outBuf  = buf.release();
    *outSize = compressedSize;
    *outMs   = elapsed;
    return ZL_ErrorCode_no_error;
}

EMSCRIPTEN_KEEPALIVE
ZL_ErrorCode openzl_wasm_benchmarkDecompress(
        const uint8_t* src,
        size_t srcSize,
        size_t iterations,
        double* outMs)
{
    if (!outMs) {
        return ZL_ErrorCode_parameter_invalid;
    }
    *outMs = 0;
    if (!src) {
        return ZL_ErrorCode_parameter_invalid;
    }
    if (iterations < 1 || iterations > OPENZL_WASM_BENCHMARK_MAX_ITERATIONS) {
        return ZL_ErrorCode_parameter_invalid;
    }

    size_t capacity = 0;
    ZL_ErrorCode code =
            openzl_wasm_getDecompressedSize(src, srcSize, &capacity);
    if (code != ZL_ErrorCode_no_error) {
        return code;
    }

    BufferPtr buf = allocBuffer(capacity);
    if (!buf) {
        return ZL_ErrorCode_allocation;
    }
    DCtxPtr dctx{ ZL_DCtx_create() };
    if (!dctx) {
        return ZL_ErrorCode_allocation;
    }

    // Warm the decompression context before timing to exclude first-call costs.
    ZL_Report first =
            ZL_DCtx_decompress(dctx.get(), buf.get(), capacity, src, srcSize);
    if (ZL_isError(first)) {
        return ZL_errorCode(first);
    }

    size_t lastSize    = 0;
    const double start = nowMs();
    for (size_t i = 0; i < iterations; i++) {
        ZL_Report r = ZL_DCtx_decompress(
                dctx.get(), buf.get(), capacity, src, srcSize);
        if (ZL_isError(r)) {
            code = ZL_errorCode(r);
            break;
        }
        lastSize = ZL_validResult(r);
    }
    const double elapsed = nowMs() - start;

    if (code != ZL_ErrorCode_no_error) {
        return code;
    }
    // The frame header promised this size, anything else means a bad round
    // trip.
    if (lastSize != capacity) {
        return ZL_ErrorCode_GENERIC;
    }

    *outMs = elapsed;
    return ZL_ErrorCode_no_error;
}
