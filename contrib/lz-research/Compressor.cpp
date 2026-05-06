// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <filesystem>

#include "Compressor.hpp"

#include <snappy.h>

#include "openzl/shared/varint.h"
#include "openzl/zl_reflection.h"
#include "tools/io/OutputFile.h"
#include "tools/training/train.h"

#include "LzCompressors.hpp"
#include "codecs/Lz.hpp"
#include "openzl/shared/xxhash.h"

namespace openzl::bench {

namespace {
namespace fs = std::filesystem;

const char kBase64EncodeTable[] = {
    'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M',
    'N', 'O', 'P', 'Q', 'R', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',
    'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i', 'j', 'k', 'l', 'm',
    'n', 'o', 'p', 'q', 'r', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',
    '0', '1', '2', '3', '4', '5', '6', '7', '8', '9', '+', '/'
};

static size_t base64EncodedSize(size_t srcSize)
{
    size_t encodedSize = (srcSize / 3) * 4;
    if (srcSize % 3 != 0) {
        encodedSize += 4;
    }
    return encodedSize;
}

static std::string base64Encode(std::string_view in)
{
    std::string out(base64EncodedSize(in.size()), '\0');

    const uint8_t* src   = (const uint8_t*)in.data();
    const size_t srcSize = in.size();
    char* dst            = out.data();

    const char* const dstBegin  = dst;
    const uint8_t* const srcEnd = src + srcSize;
    for (; (srcEnd - src) >= 3; src += 3, dst += 4) {
        dst[0] = kBase64EncodeTable[src[0] >> 2];
        dst[1] = kBase64EncodeTable[((src[0] & 0x03) << 4) + ((src[1] >> 4))];
        dst[2] = kBase64EncodeTable[((src[1] & 0x0f) << 2) + ((src[2] >> 6))];
        dst[3] = kBase64EncodeTable[src[2] & 0x3f];
    }

    if (src < srcEnd) {
        assert(src + 1 == srcEnd || src + 2 == srcEnd);
        dst[0] = kBase64EncodeTable[src[0] >> 2];
        if (src + 1 == srcEnd) {
            dst[1] = kBase64EncodeTable[(src[0] & 0x03) << 4];
            dst[2] = '=';
        } else {
            dst[1] = kBase64EncodeTable[((src[0] & 0x03) << 4) + (src[1] >> 4)];
            dst[2] = kBase64EncodeTable[(src[1] & 0x0f) << 2];
        }
        dst[3] = '=';
        dst += 4;
    }
    const size_t dstSize = (size_t)(dst - dstBegin);
    ZL_ASSERT_EQ(dstSize, base64EncodedSize(srcSize));
    return out;
}

static const uint8_t kBase64DecodeTable[256] = {
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x3e, 0xff, 0xff, 0xff, 0x3f,
    0x34, 0x35, 0x36, 0x37, 0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06,
    0x07, 0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f, 0x10, 0x11, 0x12,
    0x13, 0x14, 0x15, 0x16, 0x17, 0x18, 0x19, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f, 0x20, 0x21, 0x22, 0x23, 0x24,
    0x25, 0x26, 0x27, 0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f, 0x30,
    0x31, 0x32, 0x33, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff,
};

static std::string base64Decode(std::string_view in)
{
    while (!in.empty() && in.back() == '=') {
        in = in.substr(0, in.size() - 1);
    }

    std::string out;
    out.reserve(in.size() * 3 / 4);
    const uint8_t* const src = (const uint8_t*)in.data();
    for (size_t i = 0; i + 1 < in.size(); i += 4) {
        out += (kBase64DecodeTable[src[i]] << 2)
                | (kBase64DecodeTable[src[i + 1]] >> 4);
        if (i + 2 < in.size()) {
            out += ((kBase64DecodeTable[src[i + 1]] << 4) & 0xf0)
                    | (kBase64DecodeTable[src[i + 2]] >> 2);
            if (i + 3 < in.size()) {
                out += ((kBase64DecodeTable[src[i + 2]] << 6) & 0xc0)
                        | (kBase64DecodeTable[src[i + 3]]);
            }
        }
    }
    return out;
}

static std::string hexEncode(uint64_t in)
{
    constexpr std::array<char, 16> hex = { '0', '1', '2', '3', '4', '5',
                                           '6', '7', '8', '9', 'a', 'b',
                                           'c', 'd', 'e', 'f' };
    std::string out;
    out.reserve(16);
    for (size_t i = 0; i < 16; ++i) {
        auto nibble = (in >> (4 * i)) & 0xF;
        out.push_back(hex[nibble]);
    }
    return out;
}

size_t zstdThrowIfError(size_t result)
{
    if (ZSTD_isError(result)) {
        throw std::runtime_error(ZSTD_getErrorName(result));
    }
    return result;
}
size_t lz4ThrowIfError(int result)
{
    if (result <= 0) {
        throw std::runtime_error("LZ4 error");
    }
    return result;
}

std::string shortParam(openzl::CParam key)
{
    switch (key) {
        case openzl::CParam::CompressedChecksum:
            return "ccheck";
        case openzl::CParam::ContentChecksum:
            return "dcheck";
        case openzl::CParam::CompressionLevel:
            return "clvl";
        case openzl::CParam::DecompressionLevel:
            return "dlvl";
        case openzl::CParam::FormatVersion:
            return "format";
        case openzl::CParam::MinStreamSize:
            return "minsize";
        case openzl::CParam::PermissiveCompression:
            return "permissive";
        default:
            return std::to_string(int(key));
    }
}

std::string shortParam(ZSTD_cParameter key)
{
    switch (key) {
        case ZSTD_c_windowLog:
            return "wlog";
        case ZSTD_c_hashLog:
            return "hlog";
        case ZSTD_c_chainLog:
            return "clog";
        case ZSTD_c_minMatch:
            return "mlen";
        case ZSTD_c_targetLength:
            return "tlen";
        case ZSTD_c_strategy:
            return "strat";
        case ZSTD_c_checksumFlag:
            return "check";
        default:
            return std::to_string(key);
    }
}

openzl::CParam parseOpenZLCParam(std::string_view key)
{
    if (key == "StickyParameters") {
        throw std::runtime_error("cant set sticky");
    }
#define HANDLE(param)                 \
    if (key == #param) {              \
        return openzl::CParam::param; \
    }

    HANDLE(CompressionLevel);
    HANDLE(DecompressionLevel);
    HANDLE(FormatVersion);
    HANDLE(PermissiveCompression);
    HANDLE(CompressedChecksum);
    HANDLE(ContentChecksum);
    HANDLE(MinStreamSize);

#undef HANDLE

    throw std::runtime_error("Unknown parameter: " + std::string(key));
}

std::string param(openzl::CParam key)
{
#define HANDLE(param)           \
    case openzl::CParam::param: \
        return #param;

    switch (key) {
        HANDLE(CompressionLevel);
        HANDLE(DecompressionLevel);
        HANDLE(FormatVersion);
        HANDLE(PermissiveCompression);
        HANDLE(CompressedChecksum);
        HANDLE(ContentChecksum);
        HANDLE(MinStreamSize);
        default:
            throw std::runtime_error("Bad parameter: " + shortParam(key));
    }
#undef HANDLE
} // namespace

ZSTD_cParameter parseZstdCParam(std::string_view key)
{
    if (key == "compressionLevel") {
        throw std::runtime_error("Set level via level");
    }

#define HANDLE(param)          \
    if (key == #param) {       \
        return ZSTD_c_##param; \
    }

    HANDLE(windowLog);
    HANDLE(hashLog);
    HANDLE(chainLog);
    HANDLE(searchLog);
    HANDLE(minMatch);
    HANDLE(targetLength);
    HANDLE(strategy);
    HANDLE(targetCBlockSize);
    HANDLE(enableLongDistanceMatching);
    HANDLE(ldmHashLog);
    HANDLE(ldmHashRateLog);
    HANDLE(contentSizeFlag);
    HANDLE(checksumFlag);
    HANDLE(dictIDFlag);
    HANDLE(nbWorkers);
    HANDLE(jobSize);
    HANDLE(overlapLog);

#undef HANDLE

    throw std::runtime_error("Unknown parameter: " + std::string(key));
}
} // namespace

std::string ZstdCompressor::name() const
{
    std::string name = "zstd[";
    if (level_.has_value()) {
        name += "lvl=" + std::to_string(level_.value()) + ",";
    }
    if (!params_.empty()) {
        for (const auto& [k, v] : params_) {
            name += shortParam(k) + "=" + std::to_string(v) + ",";
        }
        name.pop_back();
        name += ",";
    }
    name.pop_back();
    if (level_.has_value() || !params_.empty()) {
        name += "]";
    }
    return name;
}

ZstdCompressor::ZstdCompressor(nlohmann::json config)
{
    if (config.contains("level")) {
        level_ = config["level"];
    }
    if (config.contains("params")) {
        for (const auto& [key, value] : config["params"].items()) {
            params_.emplace(parseZstdCParam(key), int(value));
        }
    }
}

size_t ZstdCompressor::compressBound(std::string_view data) const
{
    return ZSTD_compressBound(data.size());
}

size_t ZstdCompressor::decompressedSize(std::string_view compressed) const
{
    auto size = ZSTD_findDecompressedSize(compressed.data(), compressed.size());
    if (size == ZSTD_CONTENTSIZE_ERROR || size == ZSTD_CONTENTSIZE_UNKNOWN) {
        throw std::runtime_error("bad content size");
    }
    return size;
}

size_t ZstdCompressor::compress(
        std::span<char> compressed,
        std::string_view data)
{
    if (!cctx_) {
        cctx_.reset(ZSTD_createCCtx());
        if (!cctx_) {
            throw std::bad_alloc{};
        }

        // Set default parameters
        zstdThrowIfError(
                ZSTD_CCtx_setParameter(cctx_.get(), ZSTD_c_checksumFlag, 0));

        if (level_.has_value()) {
            zstdThrowIfError(ZSTD_CCtx_setParameter(
                    cctx_.get(), ZSTD_c_compressionLevel, level_.value_or(3)));
        }
        for (const auto& [k, v] : params_) {
            zstdThrowIfError(ZSTD_CCtx_setParameter(cctx_.get(), k, v));
        }
    }
    return zstdThrowIfError(ZSTD_compress2(
            cctx_.get(),
            compressed.data(),
            compressed.size(),
            data.data(),
            data.size()));
}

size_t ZstdCompressor::decompress(
        std::span<char> decompressed,
        std::string_view compressed)
{
    if (!dctx_) {
        dctx_.reset(ZSTD_createDCtx());
        if (!dctx_) {
            throw std::bad_alloc{};
        }
    }
    return zstdThrowIfError(ZSTD_decompressDCtx(
            dctx_.get(),
            decompressed.data(),
            decompressed.size(),
            compressed.data(),
            compressed.size()));
}

Lz4Compressor::Lz4Compressor(nlohmann::json config)
{
    if (config.contains("level")) {
        level_ = config["level"];
    }
}

std::string Lz4Compressor::name() const
{
    std::string name = "lz4";
    if (level_.has_value()) {
        name += "[lvl=" + std::to_string(level_.value()) + "]";
    }
    return name;
}

size_t Lz4Compressor::compressBound(std::string_view data) const
{
    if (data.size() == 0 || data.size() > LZ4_MAX_INPUT_SIZE) {
        throw std::runtime_error("Invalid input size");
    }
    return ZL_VARINT_LENGTH_64 + LZ4_compressBound(data.size());
}

size_t Lz4Compressor::decompressedSize(std::string_view compressed) const
{
    return readHeader(compressed);
}

size_t Lz4Compressor::writeHeader(std::span<char>& compressed, size_t size)
        const
{
    if (compressed.size() < ZL_VARINT_LENGTH_64) {
        throw std::runtime_error("Not enough space for header");
    }
    auto hSize = ZL_varintEncode(size, (uint8_t*)compressed.data());
    compressed = compressed.subspan(hSize);
    return hSize;
}

size_t Lz4Compressor::readHeader(std::string_view& compressed) const
{
    const uint8_t* p = (const uint8_t*)compressed.data();
    auto result      = ZL_varintDecode(&p, p + compressed.size());
    if (ZL_RES_isError(result)) {
        throw std::runtime_error("Invalid header");
    }
    compressed = compressed.substr(p - (const uint8_t*)compressed.data());
    return size_t(ZL_RES_value(result));
}

size_t Lz4Compressor::compress(
        std::span<char> compressed,
        std::string_view data)
{
    auto hSize = writeHeader(compressed, data.size());
    if (!level_.has_value() || level_.value() <= 1) {
        if (!cctx_) {
            cctx_ = std::make_unique<uint8_t[]>(LZ4_sizeofState());
        }
        auto accel = -level_.value_or(1);
        return hSize
                + lz4ThrowIfError(LZ4_compress_fast_extState(
                        cctx_.get(),
                        data.data(),
                        compressed.data(),
                        data.size(),
                        compressed.size(),
                        accel));
    } else {
        if (!cctx_) {
            cctx_ = std::make_unique<uint8_t[]>(LZ4_sizeofStateHC());
        }
        return hSize
                + lz4ThrowIfError(LZ4_compress_HC_extStateHC(
                        cctx_.get(),
                        data.data(),
                        compressed.data(),
                        data.size(),
                        compressed.size(),
                        level_.value()));
    }
}

size_t Lz4Compressor::decompress(
        std::span<char> decompressed,
        std::string_view compressed)
{
    readHeader(compressed);
    return lz4ThrowIfError(LZ4_decompress_safe(
            compressed.data(),
            decompressed.data(),
            compressed.size(),
            decompressed.size()));
}

SnappyCompressor::SnappyCompressor(nlohmann::json config)
{
    if (config.contains("level")) {
        level_ = config["level"];
    }
}

std::string SnappyCompressor::name() const
{
    std::string name = "snappy";
    if (level_.has_value()) {
        name += "[lvl=" + std::to_string(level_.value()) + "]";
    }
    return name;
}

size_t SnappyCompressor::compressBound(std::string_view data) const
{
    return snappy::MaxCompressedLength(data.size());
}

size_t SnappyCompressor::decompressedSize(std::string_view compressed) const
{
    size_t size;
    snappy::GetUncompressedLength(compressed.data(), compressed.size(), &size);
    return size;
}

size_t SnappyCompressor::compress(
        std::span<char> compressed,
        std::string_view data)
{
    size_t compressedSize = compressed.size();
    snappy::RawCompress(
            data.data(), data.size(), compressed.data(), &compressedSize);
    return compressedSize;
}

size_t SnappyCompressor::decompress(
        std::span<char> decompressed,
        std::string_view compressed)
{
    if (!snappy::RawUncompress(
                compressed.data(), compressed.size(), decompressed.data())) {
        throw std::runtime_error("snappy decompress failed");
    }
    return decompressedSize(compressed);
}

Lz4LikeCompressor::Lz4LikeCompressor(nlohmann::json config) {}

std::string Lz4LikeCompressor::name() const
{
    std::string name = "lz4like";
    return name;
}

size_t Lz4LikeCompressor::compressBound(std::string_view data) const
{
    return lz::Lz4Like::compressBound(data.size());
}

size_t Lz4LikeCompressor::decompressedSize(std::string_view compressed) const
{
    return lz::Lz4Like::decompressedSize(compressed);
}

size_t Lz4LikeCompressor::compress(
        std::span<char> compressed,
        std::string_view data)
{
    return lz::Lz4Like::compress(compressed, data);
}

size_t Lz4LikeCompressor::decompress(
        std::span<char> decompressed,
        std::string_view compressed)
{
    return lz::Lz4Like::decompress(decompressed, compressed);
}

namespace {
class SaveInputStreamsIntrospectionHooks
        : public openzl::CompressIntrospectionHooks {
   public:
    SaveInputStreamsIntrospectionHooks(
            std::string dir,
            std::unordered_set<std::string> nodes)
            : dir_(std::move(dir)), nodes_(std::move(nodes))
    {
    }

    void on_codecEncode_start(
            ZL_Encoder* eictx,
            const ZL_Compressor* compressor,
            ZL_NodeID nid,
            const ZL_Input* inStreams[],
            size_t nbInStreams) override
    {
        std::string name = ZL_Compressor_Node_getName(compressor, nid);
        if (!nodes_.contains(name)) {
            return;
        }
        auto out = dir_ / name;
        fs::create_directories(out);
        for (size_t i = 0; i < nbInStreams; ++i) {
            saveInStream(out, inStreams[i]);
        }
    }

    ~SaveInputStreamsIntrospectionHooks() override = default;

   private:
    void saveInStream(const fs::path& dir, const ZL_Input* in) const
    {
        XXH3_state_t state;
        XXH3_64bits_reset(&state);
        const auto type           = ZL_Input_type(in);
        const auto width          = ZL_Input_eltWidth(in);
        poly::string_view content = { (const char*)ZL_Input_ptr(in),
                                      ZL_Input_contentSize(in) };
        auto lengths              = type == ZL_Type_string
                             ? poly::string_view{ (const char*)ZL_Input_stringLens(in),
                                     ZL_Input_numElts(in) * sizeof(uint32_t) }
                             : poly::string_view{};
        XXH3_64bits_update(&state, &type, sizeof(type));
        XXH3_64bits_update(&state, &width, sizeof(width));
        XXH3_64bits_update(&state, content.data(), content.size());
        XXH3_64bits_update(&state, lengths.data(), lengths.size());
        auto hash = hexEncode(XXH3_64bits_digest(&state));

        std::string name = dir / hash;
        if (type == ZL_Type_serial) {
            name += ".serial";
        } else if (type == ZL_Type_struct) {
            name += ".struct." + std::to_string(width);
        } else if (type == ZL_Type_numeric) {
            name += ".num." + std::to_string(width);
        } else if (type == ZL_Type_string) {
            name += ".string";
            auto lensName = name + ".lengths";
            name += ".content";
            tools::io::OutputFile out(lensName);
            out.write(lengths);
        }
        tools::io::OutputFile out(name);
        out.write(content);
    }

    fs::path dir_;
    std::unordered_set<std::string> nodes_;
};
} // namespace

OpenZLCompressor::OpenZLCompressor(nlohmann::json config)
{
    if (config.contains("params")) {
        for (const auto& [key, val] : config["params"].items()) {
            params_.emplace(parseOpenZLCParam(key), int(val));
        }
    }

    if (config.contains("compressor")) {
        auto compressor = config["compressor"];
        auto data       = base64Decode(std::string(compressor));
        serializedCompressor_.assign((const char*)data.data(), data.size());
    }

    if (config.contains("compressor_name")) {
        compressorName_ = config["compressor_name"];
        if (serializedCompressor_.empty()) {
            serializedCompressor_ =
                    lz::getSerializedCompressor(compressorName_);
        }
    }

    if (config.contains("trace")) {
        tracePath_ = config["trace"];
    }
    if (config.contains("trace_streams")) {
        traceStreamsDir_ = config["trace_streams"];
    }
    if (config.contains("save_input_streams")) {
        if (tracePath_.has_value()) {
            throw std::runtime_error(
                    "Cannot save input streams when trace is enabled");
        }
        const auto& saveInputStreamsConfig = config["save_input_streams"];
        if (!saveInputStreamsConfig.contains("dir")) {
            throw std::runtime_error(
                    "Must set output `dir` for save_input_streams");
        }
        if (!saveInputStreamsConfig.contains("nodes")) {
            throw std::runtime_error("Must set `nodes` for save_input_streams");
        }
        saveInputStreamsHooks_ =
                std::make_unique<SaveInputStreamsIntrospectionHooks>(
                        saveInputStreamsConfig["dir"],
                        saveInputStreamsConfig["nodes"]
                                .get<std::unordered_set<std::string>>());
    }

    configure(compressor_);
}

std::string OpenZLCompressor::name() const
{
    std::string name = "openzl[";
    name += "c=" + compressorName_ + ",";
    for (const auto& [key, val] : params_) {
        name += shortParam(key) + "=" + std::to_string(val) + ",";
    }
    name.pop_back();
    name += "]";
    return name;
}

void OpenZLCompressor::configure(openzl::Compressor& compressor) const
{
    lz::registerGraphComponents(compressor);
    compressor.deserialize(serializedCompressor_);
}

void OpenZLCompressor::configure(openzl::CCtx& cctx) const
{
    for (const auto& [key, value] : params_) {
        cctx.setParameter(key, value);
    }
    if (tracePath_) {
        cctx.writeTraces(true);
    }
    if (saveInputStreamsHooks_ != nullptr) {
        cctx.unwrap(ZL_CCtx_attachIntrospectionHooks(
                cctx.get(), saveInputStreamsHooks_->getRawHooks()));
    }
}

void OpenZLCompressor::configure(openzl::DCtx& dctx) const
{
    lz::registerCustomCodecs(dctx);
    dctx.unwrap(ZL_DCtx_setStreamArena(dctx.get(), ZL_DataArenaType_stack));
}

size_t OpenZLCompressor::compressBound(std::string_view data) const
{
    return ZL_compressBound(data.size());
}

size_t OpenZLCompressor::decompressedSize(std::string_view data) const
{
    auto size = ZL_getDecompressedSize(data.data(), data.size());
    if (ZL_isError(size)) {
        throw std::runtime_error("bad size");
    }
    return ZL_validResult(size);
}

size_t OpenZLCompressor::compress(
        std::span<char> compressed,
        std::string_view data)
{
    if (!cctx_) {
        cctx_ = openzl::CCtx();

        // Set default parameters
        cctx_->setParameter(CParam::StickyParameters, 1);
        cctx_->setParameter(CParam::FormatVersion, ZL_MAX_FORMAT_VERSION);
        cctx_->setParameter(CParam::ContentChecksum, ZL_TernaryParam_disable);
        cctx_->setParameter(
                CParam::CompressedChecksum, ZL_TernaryParam_disable);

        configure(*cctx_);
    }
    cctx_->refCompressor(compressor_);
    auto cSize =
            cctx_->compressSerial(compressed, { data.data(), data.size() });

    if (tracePath_.has_value()) {
        auto [trace, streams] = cctx_->getLatestTrace();
        {
            tools::io::OutputFile out(*tracePath_);
            out.write(trace);
        }
        if (traceStreamsDir_.has_value()) {
            fs::create_directories(*traceStreamsDir_);
            for (const auto& [id, data] : streams) {
                {
                    tools::io::OutputFile out(
                            fs::path(*traceStreamsDir_) / (id + ".sdd"));
                    out.write(data.first);
                }
                if (!data.second.empty()) {
                    tools::io::OutputFile out(
                            fs::path(*traceStreamsDir_) / (id + ".sdlens"));
                    out.write(data.first);
                }
            }
        }
    }

    return cSize;
}

size_t OpenZLCompressor::decompress(
        std::span<char> decompressed,
        std::string_view compressed)
{
    if (!dctx_) {
        dctx_ = openzl::DCtx();
        configure(*dctx_);
    }
    return dctx_->decompressSerial(decompressed, compressed);
}

nlohmann::json OpenZLCompressor::makeConfig(
        std::string_view suffix,
        std::string_view serializedCompressor) const
{
    nlohmann::json config;
    config["algorithm"] = "openzl";
    for (const auto& [key, value] : params_) {
        config[param(key)] = value;
    }
    config["compressor_name"] = compressorName_ + "/" + std::string(suffix);
    config["compressor"]      = base64Encode(serializedCompressor);

    return config;
}

nlohmann::json OpenZLCompressor::train(
        poly::span<const std::string_view> samples) const
{
    std::vector<training::MultiInput> inputs;
    for (const auto& sample : samples) {
        training::MultiInput input;
        input.add(Input::refSerial(sample));
        inputs.push_back(std::move(input));
    }
    training::TrainParams params;
    params.compressorGenFunc = [this](poly::string_view serialized) {
        auto compressor = std::make_unique<openzl::Compressor>();
        configure(*compressor);
        compressor->deserialize(serialized);
        return compressor;
    };
    params.paretoFrontier = true;
    openzl::Compressor trainingCompressor;
    configure(trainingCompressor);
    auto compressors = training::train(inputs, trainingCompressor, params);

    nlohmann::json out = nlohmann::json::array();
    for (size_t idx = 0; idx < compressors.size(); ++idx) {
        out.push_back(makeConfig(std::to_string(idx), *compressors[idx]));
    }

    return out;
}

std::unique_ptr<Compressor> makeCompressor(nlohmann::json config)
{
    std::string algo = config["algorithm"];
    if (algo == "zstd") {
        return std::make_unique<ZstdCompressor>(config);
    } else if (algo == "lz4") {
        return std::make_unique<Lz4Compressor>(config);
    } else if (algo == "snappy") {
        return std::make_unique<SnappyCompressor>(config);
    } else if (algo == "lz4like") {
        return std::make_unique<Lz4LikeCompressor>(config);
    } else if (algo == "openzl") {
        return std::make_unique<OpenZLCompressor>(config);
    } else {
        throw std::runtime_error("Unknown algorithm: " + algo);
    }
}

} // namespace openzl::bench
