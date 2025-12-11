// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <random>
#include <vector>

#include "tests/datagen/distributions/UniformDistribution.h"
#include "tests/datagen/structures/FixedWidthDataProducer.h"

namespace openzl::tests::datagen {

class FSENCountProducer : public FixedWidthDataProducer {
   public:
    explicit FSENCountProducer(std::shared_ptr<RandWrapper> rw)
            : FixedWidthDataProducer(rw, 2), tableLog_(rw, 5, 12)
    {
    }

    FixedWidthData operator()(RandWrapper::NameType name) override
    {
        int16_t remaining = int16_t(1 << tableLog_(name));
        std::vector<int16_t> datum;
        for (size_t j = 0; j < 255 && remaining > 0; ++j) {
            const auto ncount = rw_->range(
                    "FSENCountProducer::ncount", (int16_t)-1, remaining);
            datum.push_back(ncount);
            remaining -= datum.back() == -1 ? 1 : datum.back();
        }
        if (remaining > 0) {
            datum.push_back(remaining);
        }
        return { std::string((char const*)datum.data(), datum.size() * 2), 2 };
    }

    void print(std::ostream& os) const override
    {
        os << "FSENCountProducer(std::string, 2)";
    }

   private:
    UniformDistribution<unsigned> tableLog_;
};

} // namespace openzl::tests::datagen
