// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once
#include <limits>
#include <sstream>
#include <vector>

#include "openzl/shared/overflow.h"
#include "tests/datagen/DataProducer.h"
#include "tests/datagen/distributions/UniformDistribution.h"
#include "tests/datagen/distributions/VecLengthDistribution.h"

namespace openzl::tests::datagen {

class IntegerStringProducer : public DataProducer<std::vector<std::string>> {
   public:
    explicit IntegerStringProducer(std::shared_ptr<RandWrapper> rw)
            : uniform_(rw, 0, 9),
              strLength_(rw, 1, 20),
              coinFlip_(rw, 0, 1),
              vecLength_(rw, 1),
              rw_(rw)
    {
    }

    static inline std::pair<std::string, std::vector<uint32_t>> flatten(
            const std::vector<std::string>& data)
    {
        std::string out;
        std::vector<uint32_t> fieldSizes;
        for (auto const& x : data) {
            out.append(x);
            fieldSizes.push_back(uint32_t(x.size()));
        }
        return { std::move(out), std::move(fieldSizes) };
    }

    std::vector<std::string> operator()(RandWrapper::NameType name) override
    {
        std::vector<std::string> result;
        auto len = vecLength_(name);
        for (size_t i = 0; i < len && rw_->has_more_data(); ++i) {
            auto strLen   = strLength_(name);
            bool negative = coinFlip_(name) == 0;
            int64_t value = 0;
            // Choose a length and try to choose a value that is the length
            // chosen. If it exceeds maximum, use a shorter length.
            for (size_t j = 0; j < strLen; j++) {
                uint64_t tmpValue;
                auto success = ZL_overflowMulU64(value, 10, &tmpValue);
                success |=
                        ZL_overflowAddU64(tmpValue, uniform_(name), &tmpValue);
                if (tmpValue > std::numeric_limits<int64_t>::max()
                    || !success) {
                    break;
                }
                value = tmpValue;
            }
            if (negative) {
                value *= -1;
            }
            std::stringstream ss;
            ss << value;
            result.emplace_back(ss.str());
        }
        return result;
    }

    void print(std::ostream& os) const override
    {
        os << "IntegerStringProducer(" << uniform_ << ", " << vecLength_ << ")";
    }

   private:
    UniformDistribution<uint64_t> uniform_;
    UniformDistribution<uint64_t> strLength_;
    UniformDistribution<uint64_t> coinFlip_;
    VecLengthDistribution vecLength_;
    std::shared_ptr<RandWrapper> rw_;
};

} // namespace openzl::tests::datagen
