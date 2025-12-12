// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <vector>

#include "tests/datagen/distributions/UniformDistribution.h"
#include "tests/datagen/distributions/VecLengthDistribution.h"

namespace openzl::tests::datagen {

template <typename RetType>
class VectorProducer : public DataProducer<std::vector<RetType>> {
   public:
    explicit VectorProducer(
            std::unique_ptr<DataProducer<RetType>> innerDist,
            std::unique_ptr<DataProducer<size_t>> lengthDist)
            : DataProducer<std::vector<RetType>>(),
              innerDist_(std::move(innerDist)),
              lengthDist_(std::move(lengthDist))
    {
    }

    static std::unique_ptr<VectorProducer> standardUniform(
            std::shared_ptr<RandWrapper> rw,
            RetType min,
            RetType max,
            size_t maxLength)
    {
        return std::make_unique<VectorProducer>(
                std::make_unique<UniformDistribution<RetType>>(rw, min, max),
                std::make_unique<VecLengthDistribution>(rw, 0, maxLength));
    }

    std::vector<RetType> operator()(RandWrapper::NameType name) override
    {
        std::vector<RetType> res;
        size_t const maxLength = (*lengthDist_)(name);
        res.reserve(std::max<size_t>(1, maxLength));
        for (size_t i = 0; i < maxLength; ++i) {
            res.push_back((*innerDist_)(name));
        }
        return res;
    }

    void print(std::ostream& os) const override
    {
        os << "VectorProducer(" << *innerDist_ << ", " << *lengthDist_ << ")";
    }

   private:
    std::unique_ptr<DataProducer<RetType>> innerDist_;
    std::unique_ptr<DataProducer<size_t>> lengthDist_;
};

} // namespace openzl::tests::datagen
