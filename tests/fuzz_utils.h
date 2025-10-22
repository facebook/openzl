// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <stddef.h>

#include "security/lionhead/utils/lib_ftest/fdp/fdp/fdp_impl.h"
#include "security/lionhead/utils/lib_ftest/ftest.h" // FUZZ_F

#include "tests/datagen/DataGen.h"
#include "tests/datagen/random_producer/LionheadFDPWrapper.h"

namespace zstrong::tests {
using namespace facebook::security::lionhead::fdp;

template <class HarnessMode>
datagen::DataGen fromFDP(StructuredFDP<HarnessMode>& fdp)
{
    std::shared_ptr<datagen::RandWrapper> rand = std::make_shared<
            datagen::LionheadFDPWrapper<StructuredFDP<HarnessMode>>>(fdp);
    return datagen::DataGen(rand);
}

constexpr size_t kDefaultMaxShortInputLength = 512;
constexpr size_t kDefaultMaxInputLength      = size_t(1) << 17;

/**
 * Cribbed of VecLength, but slightly more skewed towards longer lengths.
 * @return a length *in bytes*, not in elements!
 */
class BitInputLengthInBytes {
    size_t eltBitWidth;
    size_t max;

   public:
    /**
     * @param eltBitWidth the width of each element in *bits* (not bytes!)
     * @param max the maximum length in number of elements
     */
    explicit BitInputLengthInBytes(
            size_t eltBitWidth,
            size_t max = kDefaultMaxInputLength)
            : eltBitWidth(eltBitWidth), max(max)
    {
    }

    using res_type = size_t;

    template <class Mode>
    size_t gen(typename Mode::NameType name, StructuredFDP<Mode>& fdp) const
    {
        auto _guard = fdp.start_obj(
                Mode::format("type_zstrong_input_length_{}", name));
        size_t len_val     = (size_t)fdp.u16("raw_length");
        uint8_t op         = fdp.u8("length_variant");
        size_t max_nb_elts = (fdp.remaining_input_length() * 8) / eltBitWidth;
        size_t nb_elts     = std::min(this->max, max_nb_elts);
        size_t nb_bytes =
                (limit_length(op, len_val, nb_elts) * eltBitWidth + 7) / 8;
        if constexpr (has_pretty_print<size_t, Mode>::value) {
            fdp.log_mode->pretty_print(name, nb_bytes);
        }
        return nb_bytes;
    }

    static size_t limit_length(uint8_t op, size_t len_val, size_t max)
    {
        if (max == 0) {
            return 0;
        }
        // 128 / 256 = 50%
        if (op < 0b10000000) {
            return len_val % std::min(size_t(16), max);
        }
        // 64 / 256 = 25%
        if (op < 0b11000000) {
            return len_val % std::min(size_t(256), max);
        }
        // 48 / 256 = 18.75%
        if (op < 0b11110000) {
            return len_val % std::min(size_t(1024), max);
        }
        // 15 / 256 ~= 5.85%
        if (op < 0b11111111) {
            return len_val % std::min(size_t(4096), max);
        }
        return len_val % std::min(size_t(1) << 17, max);
    }
};

// typedef BitInputLengthInBytes InputBitLength; // for compatibility, for
// now typedef InputLengthInBytes InputLength; // for compatibility, for now

/**
 * A specialization of InputLengthInBytes that takes input eltWidth in
 * bytes, not bits.
 */
class InputLengthInBytes : public BitInputLengthInBytes {
   public:
    explicit InputLengthInBytes(
            size_t eltWidth,
            size_t max = kDefaultMaxInputLength)
            : BitInputLengthInBytes(eltWidth * 8, max)
    {
    }

    template <class Mode>
    size_t gen(typename Mode::NameType name, StructuredFDP<Mode>& fdp) const
    {
        return this->BitInputLengthInBytes::gen(name, fdp);
    }
};

class InputLengthInElts : public BitInputLengthInBytes {
   public:
    explicit InputLengthInElts(
            size_t eltWidth,
            size_t max = kDefaultMaxInputLength)
            : BitInputLengthInBytes(eltWidth * 8, max), eltWidth_(eltWidth)
    {
    }

    template <class Mode>
    size_t gen(typename Mode::NameType name, StructuredFDP<Mode>& fdp) const
    {
        return this->BitInputLengthInBytes::gen(name, fdp) / eltWidth_;
    }

   private:
    size_t eltWidth_;
};

class ShortInputLengthInBytes {
    size_t eltWidth;
    VecLength dist;

   public:
    using res_type = size_t;

    explicit ShortInputLengthInBytes(
            size_t eltWidth,
            size_t max = kDefaultMaxShortInputLength)
            : eltWidth(eltWidth), dist(max)
    {
    }

    template <class Mode>
    size_t gen(typename Mode::NameType name, StructuredFDP<Mode>& fdp) const
    {
        return dist.gen(name, fdp) * eltWidth;
    }
};

class ShortInputLengthInElts : public VecLength {
   public:
    using res_type = size_t;

    explicit ShortInputLengthInElts(size_t max = kDefaultMaxShortInputLength)
            : VecLength(max)
    {
    }
};

template <typename Mode, typename LenDist>
std::string
gen_str(StructuredFDP<Mode>& fdp, typename Mode::NameType name, LenDist d)
{
    return StringDistribution<Uniform<char>, LenDist>({}, d).gen(name, fdp);
}

template <typename Mode, typename LenDist>
std::string gen_str(
        StructuredFDP<Mode>& fdp,
        typename Mode::NameType name,
        LenDist d,
        std::vector<std::string> const& examples)
{
    return StringDistribution<Uniform<char>, LenDist>({}, d)
            .with_examples(examples)
            .gen(name, fdp);
}

template <typename T, typename Mode, typename LenDist>
std::vector<T>
gen_vec(StructuredFDP<Mode>& fdp, typename Mode::NameType name, LenDist d)
{
    return VecDistribution<Uniform<T>, LenDist>({}, d).gen(name, fdp);
}

/// Splits the input into segments for SplitN transform
template <typename FDP>
std::vector<size_t> getSplitNSegments(
        FDP& f,
        size_t srcSize,
        bool lastZero      = true,
        size_t maxSegments = 512)
{
    size_t numSegments = f.usize_range(
            "num_segments",
            0,
            std::min(maxSegments, std::max<size_t>(srcSize, 10)));
    std::vector<size_t> segmentSizes;
    size_t totalSize = 0;
    segmentSizes.reserve(numSegments);
    for (size_t i = 0; i < numSegments; ++i) {
        size_t const segmentSize =
                f.usize_range("segment_size", 0, srcSize - totalSize);
        segmentSizes.push_back(segmentSize);
        totalSize += segmentSize;
    }
    if (totalSize < srcSize) {
        if (lastZero) {
            segmentSizes.push_back(0);
        } else {
            segmentSizes.push_back(srcSize - totalSize);
        }
    }
    return segmentSizes;
}

} // namespace zstrong::tests
