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
                "The chunk size for the input to be separated into (e.g. 20M, 500K, 1G). Supports suffixes: K/KB (10^3), M/MB (10^6), G/GB (10^9), T/TB (10^12), and binary KiB (2^10), MiB (2^20), GiB (2^30), TiB (2^40). Plain numbers are treated as bytes. Defaults to 20M if segmenter exists for profile.");
    }

    explicit ProfileArgs(const arg::ParsedArgs& parsed)
    {
        auto chunkSize = parsed.globalFlag(kChunkSize);
        if (chunkSize.has_value()) {
            chunkSize_ = parseSize(chunkSize.value());
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
    inline static const std::string kChunkSize  = "chunk-size";
    inline static const std::string kProfile    = "profile";

    static size_t suffixToMultiplier(const std::string& suffix)
    {
        if (suffix == "K" || suffix == "KB") {
            return 1000ULL;
        } else if (suffix == "M" || suffix == "MB") {
            return 1000ULL * 1000;
        } else if (suffix == "G" || suffix == "GB") {
            return 1000ULL * 1000 * 1000;
        } else if (suffix == "T" || suffix == "TB") {
            return 1000ULL * 1000 * 1000 * 1000;
        } else if (suffix == "KiB") {
            return 1ULL << 10;
        } else if (suffix == "MiB") {
            return 1ULL << 20;
        } else if (suffix == "GiB") {
            return 1ULL << 30;
        } else if (suffix == "TiB") {
            return 1ULL << 40;
        }
        throw std::invalid_argument(
                "Unknown size suffix '" + suffix
                + "'. Use K/KB/KiB, M/MB/MiB, G/GB/GiB, or T/TB/TiB.");
    }

    static size_t parseSize(const std::string& str)
    {
        if (str.empty()) {
            throw std::invalid_argument("Size string must not be empty.");
        }
        size_t pos   = 0;
        size_t value = std::stoull(str, &pos);
        if (pos < str.size()) {
            std::string suffix = str.substr(pos);
            size_t multiplier  = suffixToMultiplier(suffix);
            if (value > SIZE_MAX / multiplier) {
                throw std::overflow_error(
                        "Size value overflows: '" + str + "'.");
            }
            value *= multiplier;
        }
        return value;
    }

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
