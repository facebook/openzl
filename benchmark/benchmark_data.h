// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <assert.h>
#include <algorithm>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include <fmt/format.h>

#include "benchmark/benchmark_data_utils.h"
#include "tests/datagen/structures/FixedWidthDataProducer.h"

namespace zstrong::bench {

/**
 * BenchmarkData:
 * Defines an interface for data that can be used by benchmarking testcases.
 * It allows to define and load the data once and reuse it in multiple
 * testcases.
 */
class BenchmarkData {
   public:
    virtual ~BenchmarkData()        = default;
    virtual std::string_view data() = 0;
    virtual std::string name()      = 0;
    virtual size_t size()
    {
        return data().size() / width();
    }
    virtual size_t width()
    {
        return 1;
    }
};

/**
 * Stores arbitrary bytes.
 */
class ArbitrarySerializedData : public BenchmarkData {
   public:
    explicit ArbitrarySerializedData(std::string data) : data_(std::move(data))
    {
    }

    std::string_view data() override
    {
        return data_;
    }

    std::string name() override
    {
        return fmt::format("Arbitrary(size={})", size());
    }

   private:
    std::string data_;
};

/**
 * Stores arbitrary variable size fields.
 */
class ArbitraryStringData : public BenchmarkData {
   public:
    explicit ArbitraryStringData(
            std::string content,
            std::vector<uint32_t> fieldSizes)
            : content_(std::move(content)), fieldSizes_(std::move(fieldSizes))
    {
    }

    std::string_view data() override
    {
        return content_;
    }

    ZL_SetStringLensInstructions getFieldSizes(void)
    {
        return { fieldSizes_.data(), fieldSizes_.size() };
    }

    std::string name() override
    {
        return fmt::format(
                "ArbitraryString(contentSize={}, nbFields={})",
                size(),
                fieldSizes_.size());
    }

   private:
    std::string content_;
    std::vector<uint32_t> fieldSizes_;
};

/**
 * ConstantData:
 * Represents a constant string, which repeats a single token many times
 */
class ConstantData : public BenchmarkData {
   private:
    std::string data_;
    size_t width_;

   public:
    ConstantData(size_t nbElts, size_t eltWidth)
            : data_(std::string(nbElts * eltWidth, 'a')), width_(eltWidth)
    {
    }
    std::string_view data() override
    {
        return data_;
    }
    size_t width() override
    {
        return width_;
    }
    std::string name() override
    {
        return fmt::format(
                "Constant(nbElts={}, eltWidth={})",
                data_.size() / width(),
                width());
    }
};

/**
 * MostlyConstantData:
 * Represents a mostly constant string which is very skewed to one character
 * that repeats itself and a couple of other characters.
 */
class MostlyConstantData : public BenchmarkData {
   private:
    std::string data_;

   public:
    MostlyConstantData() : data_("MostlyConstant" + std::string(10000, 'a')) {}
    std::string_view data() override
    {
        return data_;
    }
    std::string name() override
    {
        return "MostlyConstant";
    }
};

/**
 * FixedSizeData:
 * Generates a random string of @p nbElts * @p eltWidth bytes from a uniform
 * distribution
 * @p nbElts The number of elements in the generated buffer
 * @p eltWidth The width of each element in the generated buffer
 * @p seed The seed for generating the random values in the buffer
 */
class FixedSizeData : public BenchmarkData {
   private:
    size_t const eltWidth_;
    size_t const seed_;
    std::vector<uint8_t> data_;

   public:
    FixedSizeData(size_t nbElts, size_t eltWidth, size_t seed = 10)
            : eltWidth_(eltWidth), seed_(seed)
    {
        data_ = utils::generateUniformRandomVector<uint8_t>(
                nbElts * eltWidth_, seed_, 0, 255);
    }
    std::string_view data() override
    {
        return utils::getStringView(data_);
    }
    size_t width() override
    {
        return eltWidth_;
    }
    std::string name() override
    {
        return fmt::format(
                "FixedSizeUniform(nbElts={}, eltWidth={})",
                data_.size() / eltWidth_,
                eltWidth_);
    }
};

/**
 * VariableSizeData:
 * Generates a random input of length `nbElts` and a random vector of field
 * sizes that sum to `nbElts`. The vector's elements can be used to parse the
 * input into "fields" that mock a variable-size field input
 *
 * Conditions:
 * - @p nbElts >= @p maxSegLen
 * - @p minSegLen <= @p maxSegLen
 * - @p minSegLen >= @p nbElts % @p maxSegLen
 *
 * @param sorted Whether to sort the generated data or not
 * @param nbElts The number of characters in the generated buffer
 * @param minSegLen The minimum length of a segment
 * @param maxSegLen The maximum length of a segment
 * @param alphabetSize The size of the alphabet to draw characters from
 * @param seed The seed for generating the random values in the buffer
 */
class VariableSizeData : public BenchmarkData {
   private:
    std::vector<uint8_t> data_;
    std::vector<uint32_t> fieldSizes_;
    std::vector<std::string> fields_;
    bool const sorted_;
    uint32_t const minSegLen_;
    uint32_t const maxSegLen_;
    uint8_t const alphabetSize_;

   public:
    explicit VariableSizeData(
            bool sorted,
            size_t const nbBytes,
            uint32_t const minSegLen,
            uint32_t const maxSegLen,
            uint8_t const alphabetSize,
            size_t const seed = 10)
            : sorted_(sorted),
              minSegLen_(minSegLen),
              maxSegLen_(maxSegLen),
              alphabetSize_(alphabetSize)
    {
        // Generate bytes and field sizes
        std::vector<uint8_t> bytes =
                utils::generateUniformRandomVector<uint8_t>(
                        nbBytes, seed, 0, alphabetSize - 1);
        fieldSizes_ = utils::genStringLens(nbBytes, minSegLen, maxSegLen, seed);

        if (!sorted) {
            data_ = bytes;
        } else {
            // Construct strings from bytes
            size_t const nbFields = fieldSizes_.size();
            fields_               = std::vector<std::string>(nbFields);
            auto nextFieldStart   = bytes.begin();
            for (size_t i = 0; i < nbFields; ++i) {
                uint32_t const fieldSize = fieldSizes_[i];
                fields_[i] =
                        std::string(nextFieldStart, nextFieldStart + fieldSize);
                nextFieldStart += fieldSize;
            }

            // Sort fields lexicographically
            std::sort(fields_.begin(), fields_.end());
            for (size_t i = 0; i < nbFields; ++i) {
                fieldSizes_[i] = (uint32_t)fields_[i].length();
            }

            // Store strings as bytes
            for (const auto& s : fields_) {
                data_.insert(data_.end(), s.begin(), s.end());
            }
        }
    }
    ZL_SetStringLensInstructions getFieldSizes(void)
    {
        return { fieldSizes_.data(), fieldSizes_.size() };
    }
    std::string_view data() override
    {
        return utils::getStringView(data_);
    }
    size_t width() override
    {
        return 1;
    }
    std::string name() override
    {
        return fmt::format(
                "{}Variable(nbBytes={}, nbSegments={}, minSegLength={}, maxSegLength={}, alphabetSize={})",
                sorted_ ? "Sorted" : "Unsorted",
                data_.size(),
                fieldSizes_.size(),
                minSegLen_,
                maxSegLen_,
                alphabetSize_);
    }
};

/**
 * UniformDistributionData:
 * Defines a data buffer composed from multiple samples of LE encoded integers
 * of type T taken from a uniform distribution with given cardinality.
 * The range of data samples is 0 to cardinality-1.
 */
template <typename T>
class UniformDistributionData : public BenchmarkData {
   private:
    std::optional<T> cardinality_;
    size_t size_;
    std::optional<T> min_;
    std::optional<T> max_;
    size_t seed_;
    std::vector<uint8_t> data_;

   public:
    UniformDistributionData(
            size_t size,
            std::optional<T> cardinality,
            std::optional<T> min = {},
            std::optional<T> max = {},
            size_t seed          = 10)
            : cardinality_(cardinality),
              size_(size),
              min_(min),
              max_(max),
              seed_(seed)
    {
        auto const minValue = min.value_or(std::numeric_limits<T>::min());
        auto const maxValue = max.value_or(std::numeric_limits<T>::max());
        if (cardinality.has_value()) {
            auto alphabet = utils::generateRandomAlphabet<T>(
                    cardinality.value(), seed, minValue, maxValue);
            data_ = utils::toUint8Vector(
                    utils::generateUniformRandomVector<T>(
                            size, seed, alphabet));
        } else {
            data_ = utils::toUint8Vector(
                    utils::generateUniformRandomVector<T>(
                            size, seed, minValue, maxValue));
        }
    }
    std::string_view data() override
    {
        return utils::getStringView(data_);
    }
    size_t width() override
    {
        return sizeof(T);
    }
    std::string name() override
    {
        std::string cardinalityStr, minStr, maxStr;
        if (cardinality_.has_value()) {
            cardinalityStr = fmt::format("card={}, ", cardinality_.value());
        }
        if (min_.has_value()) {
            minStr = fmt::format("min={}, ", min_.value());
        }
        if (max_.has_value()) {
            maxStr = fmt::format("max={}, ", max_.value());
        }
        return fmt::format(
                "Uniform{}({}{}{}size={}, seed={})",
                sizeof(T) * 8,
                cardinalityStr,
                minStr,
                maxStr,
                size_,
                seed_);
    }
};

/**
 * Generates a vector containing @p numRuns sorted runs in increasing order.
 * The run lengths are normally distributed around @p avgRunLength.
 * Between all the runs there are @p numUniqueValues unique values.
 */
template <typename T>
class SortedRunsData : public BenchmarkData {
   private:
    size_t numRuns_;
    size_t avgRunLength_;
    size_t numUniqueValues_;

    size_t seed_;
    std::vector<uint8_t> data_;

   public:
    SortedRunsData(
            size_t numRuns,
            size_t avgRunLength,
            size_t numUniqueValues,
            size_t seed = 10)
            : numRuns_(numRuns),
              avgRunLength_(avgRunLength),
              numUniqueValues_(numUniqueValues),
              seed_(seed)
    {
        assert(numRuns >= 1);
        auto alphabet = utils::generateRandomAlphabet<T>(numUniqueValues, seed);
        data_         = utils::toUint8Vector(
                utils::genSortedRuns(alphabet, numRuns_, avgRunLength_, seed));
    }
    std::string_view data() override
    {
        return utils::getStringView(data_);
    }
    size_t width() override
    {
        return sizeof(T);
    }
    std::string name() override
    {
        return fmt::format(
                "SortedRuns{}(numRuns={}, avgRunLength={}, numUniqueValues={}, seed={})",
                sizeof(T) * 8,
                numRuns_,
                avgRunLength_,
                numUniqueValues_,
                seed_);
    }
};

/**
 * NormalDistributionData:
 * Defines a data buffer composed from multiple samples of LE encoded integers
 * of type T taken from a clamped normal distribution.
 * The normal distribution is defined by a mean and standard deviation and
 * values are clamped to fit in type T.
 * Clamping is done by resampling values outside of range, distribution
 * parameters that are mostly out of range will result in longer generation
 * times and are discouraged.
 */
template <typename T>
class NormalDistributionData : public BenchmarkData {
   private:
    double mean_;
    double stddev_;
    size_t size_;
    size_t seed_;
    std::vector<uint8_t> data_;

   public:
    NormalDistributionData(
            double mean,
            double stddev,
            size_t size,
            size_t seed = 10)
            : mean_(mean),
              stddev_(stddev),
              size_(size),
              seed_(seed),
              data_(utils::toUint8Vector(
                      utils::generateNormalRandomVector<T>(
                              size,
                              seed,
                              mean,
                              stddev)))
    {
    }
    std::string_view data() override
    {
        return utils::getStringView(data_);
    }
    size_t width() override
    {
        return sizeof(T);
    }
    std::string name() override
    {
        return fmt::format(
                "Normal{}(mean={}, stddev={}, size={}, seed={})",
                sizeof(T) * 8,
                mean_,
                stddev_,
                size_,
                seed_);
    }
};

/**
 * File:
 * Uses the content of a file as data for benchmarks.
 * Files are always assumed to be a serialized format.
 */
class FileData : public BenchmarkData {
   private:
    std::string data_;
    std::string path_;

   public:
    FileData(std::string path) : data_(utils::readCorpus(path)), path_(path) {}
    std::string_view data() override
    {
        return data_;
    }
    std::string name() override
    {
        return fmt::format("File({})", path_);
    }
};

/**
 * Generates benchmark data according to a custom random generator function
 */
template <typename T>
class CustomDistributionData : public BenchmarkData {
   private:
    using CustomGeneration =
            std::function<const std::vector<T>(size_t, size_t)>;
    size_t seed_;
    size_t size_;
    std::vector<uint8_t> data_;
    CustomGeneration custom_;

   public:
    CustomDistributionData(
            size_t size,
            CustomGeneration custom,
            size_t seed = 10)
            : seed_(seed), size_(size), custom_(custom)
    {
        auto alphabet = utils::generateRandomAlphabet<T>(size, seed);
        data_         = utils::toUint8Vector(custom_(size, seed));
    }
    std::string_view data() override
    {
        return utils::getStringView(data_);
    }
    size_t width() override
    {
        return sizeof(T);
    }
    std::string name() override
    {
        return fmt::format(
                "Custom run (size={}, seed={})", sizeof(T) * 8, size_, seed_);
    }
};

class FixedWidthDataProducerData : public BenchmarkData {
   public:
    explicit FixedWidthDataProducerData(
            openzl::tests::datagen::FixedWidthDataProducer& producer)
            : data_(producer("FixedWidthDataProducerData"))
    {
        std::ostringstream oss;
        producer.print(oss);
        name_ = oss.str();
    }

    std::string_view data() override
    {
        return data_.data;
    }
    size_t width() override
    {
        return data_.width;
    }
    std::string name() override
    {
        return name_;
    }

   private:
    openzl::tests::datagen::FixedWidthData data_;
    std::string name_;
};

} // namespace zstrong::bench
