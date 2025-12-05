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

// Forward declaration
namespace util {
unsigned long long parseStrictULL(const std::string& str);
}

class ProfileArgs {
   public:
    static void addArgs(arg::ArgParser& parser)
    {
        parser.addGlobalFlag(kProfile, 'p', true, "Select the given profile.");
        parser.addGlobalFlag(
                kProfileArg,
                0,
                true,
                "Pass the given value as an argument to constructing the profile.");
        parser.addGlobalFlag(
                kChunkSize,
                0,
                true,
                "The chunk size in MB for the input to be separated into. This reduces the memory usage to a multiplicative factor of the chunk size instead of the whole input. Defaults to 20MB if segmenter exists for profile.");
    }

    explicit ProfileArgs(const arg::ParsedArgs& parsed)
    {
        auto chunkSize = parsed.globalFlag(kChunkSize);
        if (chunkSize.has_value()) {
            chunkSize_ = util::parseStrictULL(chunkSize.value()) * 1000 * 1000;
        } else {
            chunkSize_ = poly::nullopt;
        }
        auto profileArg = parsed.globalFlag(kProfileArg);
        if (profileArg) {
            argmap_.emplace("TBD", profileArg.value());
        }
        auto profile = parsed.globalFlag(kProfile);
        if (profile) {
            name_ = std::move(profile);
        }
    }

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
    inline static const std::string kChunkSize  = "chunk-size-mb";
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
            std::shared_ptr<void> opaque_ = nullptr)
            : name(name_),
              description(description_),
              gen(std::move(gen_)),
              opaque(opaque_)
    {
    }

    virtual ~CompressProfile() = default;

    std::string name;
    std::string description; // useful for documentation as well as printing
    GenFunc gen;
    std::shared_ptr<void> opaque; // an optional opaque helper pointer
                                  // that's passed to the gen function
};

const std::map<std::string, std::shared_ptr<CompressProfile>>&
compressProfiles();

} // namespace openzl::cli
