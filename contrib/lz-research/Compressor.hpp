// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>

#include <lz4.h>
#include <lz4hc.h>
#include <zstd.h>

#include "openzl/openzl.hpp"
#include "tools/json.hpp"

namespace openzl::bench {
class Compressor {
   public:
    virtual std::string name() const                                   = 0;
    virtual size_t compressBound(std::string_view data) const          = 0;
    virtual size_t decompressedSize(std::string_view compressed) const = 0;
    virtual size_t compress(
            std::span<char> compressed,
            std::string_view data) = 0;
    virtual size_t decompress(
            std::span<char> decompressed,
            std::string_view compressed) = 0;
    virtual nlohmann::json train(
            poly::span<const std::string_view> samples) const
    {
        throw std::runtime_error("Training not supported");
    }

    std::string compress(std::string_view data)
    {
        std::string compressed;
        compressed.resize(compressBound(data));
        auto size = compress(compressed, data);
        compressed.resize(size);
        return compressed;
    }

    std::string decompress(std::string_view compressed)
    {
        std::string decompressed;
        decompressed.resize(decompressedSize(compressed));
        auto size = decompress(decompressed, compressed);
        decompressed.resize(size);
        return decompressed;
    }

    virtual ~Compressor() = default;
};

class ZstdCompressor : public Compressor {
   public:
    explicit ZstdCompressor(int level) : level_(level) {}
    explicit ZstdCompressor(std::unordered_map<ZSTD_cParameter, int> params)
            : params_(std::move(params))
    {
    }
    ZstdCompressor(
            std::optional<int> level,
            std::unordered_map<ZSTD_cParameter, int> params)
            : level_(level), params_(std::move(params))
    {
    }
    explicit ZstdCompressor(nlohmann::json config);

    std::string name() const override;
    size_t compressBound(std::string_view data) const override;
    size_t decompressedSize(std::string_view compressed) const override;
    size_t compress(std::span<char> compressed, std::string_view data) override;
    size_t decompress(std::span<char> decompressed, std::string_view compressed)
            override;

    virtual ~ZstdCompressor() = default;

   private:
    struct CCtxDeleter {
        void operator()(ZSTD_CCtx* ctx) const
        {
            ZSTD_freeCCtx(ctx);
        }
    };
    using ZstdCCtx = std::unique_ptr<ZSTD_CCtx, CCtxDeleter>;
    struct DCtxDeleter {
        void operator()(ZSTD_DCtx* ctx) const
        {
            ZSTD_freeDCtx(ctx);
        }
    };
    using ZstdDCtx = std::unique_ptr<ZSTD_DCtx, DCtxDeleter>;

    std::optional<int> level_;
    std::unordered_map<ZSTD_cParameter, int> params_;
    ZstdCCtx cctx_;
    ZstdDCtx dctx_;
};

class Lz4Compressor : public Compressor {
   public:
    explicit Lz4Compressor(std::optional<int> level) : level_(level) {}
    explicit Lz4Compressor(nlohmann::json config);

    std::string name() const override;
    size_t compressBound(std::string_view data) const override;
    size_t decompressedSize(std::string_view compressed) const override;
    size_t compress(std::span<char> compressed, std::string_view data) override;
    size_t decompress(std::span<char> decompressed, std::string_view compressed)
            override;

    virtual ~Lz4Compressor() = default;

   private:
    size_t writeHeader(std::span<char>& compressed, size_t size) const;
    size_t readHeader(std::string_view& compressed) const;

    std::optional<int> level_;
    std::unique_ptr<uint8_t[]> cctx_;
};

class SnappyCompressor : public Compressor {
   public:
    explicit SnappyCompressor(std::optional<int> level) : level_(level) {}
    explicit SnappyCompressor(nlohmann::json config);

    std::string name() const override;
    size_t compressBound(std::string_view data) const override;
    size_t decompressedSize(std::string_view compressed) const override;
    size_t compress(std::span<char> compressed, std::string_view data) override;
    size_t decompress(std::span<char> decompressed, std::string_view compressed)
            override;

    virtual ~SnappyCompressor() = default;

    std::optional<int> level_;
};

class Lz4LikeCompressor : public Compressor {
   public:
    explicit Lz4LikeCompressor() {}
    explicit Lz4LikeCompressor(nlohmann::json config);

    std::string name() const override;
    size_t compressBound(std::string_view data) const override;
    size_t decompressedSize(std::string_view compressed) const override;
    size_t compress(std::span<char> compressed, std::string_view data) override;
    size_t decompress(std::span<char> decompressed, std::string_view compressed)
            override;

    virtual ~Lz4LikeCompressor() = default;
};

class OpenZLCompressor : public Compressor {
   public:
    OpenZLCompressor(
            std::string compressorName,
            openzl::Compressor compressor,
            std::unordered_map<openzl::CParam, int> params)
            : compressorName_(std::move(compressorName)),
              compressor_(std::move(compressor)),
              params_(std::move(params))
    {
    }

    explicit OpenZLCompressor(nlohmann::json config);

    std::string name() const override;

    size_t compressBound(std::string_view data) const override;
    size_t decompressedSize(std::string_view compressed) const override;
    size_t compress(std::span<char> compressed, std::string_view data) override;
    size_t decompress(std::span<char> decompressed, std::string_view compressed)
            override;

    void configure(openzl::Compressor& compressor) const;
    void configure(openzl::CCtx& cctx) const;
    void configure(openzl::DCtx& dctx) const;

    nlohmann::json train(
            poly::span<const std::string_view> samples) const override;

    virtual ~OpenZLCompressor() = default;

   private:
    nlohmann::json makeConfig(
            std::string_view suffix,
            std::string_view serializedCompressor) const;

    std::string compressorName_;
    std::string serializedCompressor_;
    openzl::Compressor compressor_;
    std::unordered_map<openzl::CParam, int> params_;
    std::optional<openzl::CCtx> cctx_;
    std::optional<openzl::DCtx> dctx_;
    std::optional<std::string> tracePath_;
    std::optional<std::string> traceStreamsDir_;
};

std::unique_ptr<Compressor> makeCompressor(nlohmann::json config);

} // namespace openzl::bench
