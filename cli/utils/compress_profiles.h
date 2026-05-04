// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/poly/Optional.hpp"

#include "openzl/zl_compressor.h"
#include "tools/arg/arg_parser.h"
#include "tools/arg/parsed_args.h"

namespace openzl::cli {

class ProfileArgs {
   public:
    static void addArgs(arg::ArgParser& parser);

    explicit ProfileArgs(const arg::ParsedArgs& parsed);

    explicit ProfileArgs(const std::shared_ptr<Compressor>& compressor)
            : compressor_(compressor)
    {
    }

    const poly::optional<size_t>& chunkSize() const
    {
        return chunkSize_;
    }

    const poly::optional<std::string>& name() const
    {
        return name_;
    }

    const std::map<std::string, std::string>& map() const
    {
        return argmap_;
    }

    const std::shared_ptr<Compressor>& compressor() const
    {
        return compressor_;
    }

    void setCompressor(const std::shared_ptr<Compressor>& compressor)
    {
        compressor_ = compressor;
    }

   private:
    std::shared_ptr<Compressor> compressor_;

    inline static const std::string kProfileArg = "profile-arg";
    inline static const std::string kChunkSize  = "chunk-size";
    inline static const std::string kProfile    = "profile";

    poly::optional<std::string> name_;
    poly::optional<size_t> chunkSize_;
    // Arbitrary (K,V) arguments provided on the command line.
    std::map<std::string, std::string> argmap_;
};

class CompressProfile {
   private:
    using GenFunc = std::function<
            ZL_GraphID(ZL_Compressor*, void*, const ProfileArgs&)>;

   public:
    CompressProfile(
            const std::string& name_,
            const std::string& description_,
            GenFunc gen_,
            std::shared_ptr<void> opaque_ = nullptr,
            bool supportsChunkSize_       = false)
            : name(name_),
              description(description_),
              gen(std::move(gen_)),
              opaque(opaque_),
              supportsChunkSize(supportsChunkSize_)
    {
    }

    virtual ~CompressProfile() = default;

    std::string name;
    std::string description; // useful for documentation as well as printing
    GenFunc gen;
    std::shared_ptr<void> opaque; // an optional opaque helper pointer
                                  // that's passed to the gen function
    bool supportsChunkSize = false;
};

const std::map<std::string, std::shared_ptr<CompressProfile>>&
compressProfiles();

} // namespace openzl::cli
