// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tests/datagen/DataProducer.h"
#include "tests/datagen/distributions/StringLengthDistribution.h"

namespace openzl::tests::datagen {

class StringProducer : public DataProducer<std::string> {
   public:
    explicit StringProducer(std::shared_ptr<RandWrapper> generator)
            : DataProducer<std::string>(),
              rw_(generator),
              lengthDist_(generator)
    {
    }

    std::string operator()(RandWrapper::NameType name) override
    {
        return operator()(name, 1);
    }

    /**
     * Helper gen function to generate strings with a length divisible by @p
     * quantizationBytes.
     */
    std::string operator()(RandWrapper::NameType name, size_t quantizationBytes)
    {
        auto len = lengthDist_(name);
        len      = (len / quantizationBytes) * quantizationBytes;
        std::string str(len, '\0');
        for (size_t i = 0; i < len; ++i) {
            str[i] = static_cast<char>(rw_->u8("StringProducer:char"));
        }
        return str;
    }

    void print(std::ostream& os) const override
    {
        os << "StringProducer(" << lengthDist_ << ")";
    }

   private:
    std::shared_ptr<RandWrapper> rw_;
    StringLengthDistribution lengthDist_;
};

} // namespace openzl::tests::datagen
