// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <assert.h>

#include "tests/datagen/DataProducer.h"
#include "tests/datagen/distributions/StringLengthDistribution.h"

namespace openzl::tests::datagen {

class CompressibleStringProducer : public DataProducer<std::string> {
   public:
    explicit CompressibleStringProducer(
            std::shared_ptr<RandWrapper> rw,
            size_t size,
            double matchProb = 0.5)
            : DataProducer<std::string>(),
              rw_(rw),
              size_(size),
              matchProb_(matchProb)
    {
        if (matchProb < 0.0 || matchProb > 1.0) {
            throw std::invalid_argument(
                    "matchProb must be between 0.0 and 1.0");
        }
    }

    std::string operator()(RandWrapper::NameType name) override
    {
        // The number of literals to choose from.
        int const numLiterals =
                std::max(1, std::min(95, int(4.5 / (matchProb_ + 0.001))));
        int const matchProb = int(matchProb_ * 100);
        std::string output;
        output.resize(size_);
        if (output.empty()) {
            return output;
        }
        // Generate the first character so we can potentially match it
        auto it = output.begin();
        *it++   = randChar(numLiterals);

        uint32_t prevOffset = 1;
        for (auto const end = output.end(); it != end;) {
            if (!rw_->has_more_data()) {
                break;
            }
            // Pick the segment length
            uint32_t const length = randLength();
            // Either a match or literals
            if (rw_->u8_range("is_match", 0, 99) < matchProb) {
                // copy up to length bytes
                auto const copyEnd = std::min(it + length, end);
                // Use a previous offset 1/16 of the time
                bool const repeatOffset =
                        (rw_->u8_range("repeat_offset", 0, 15) == 0);
                // Pick a random offset <= 32 KB away
                uint32_t const maxOffset =
                        std::min(0x7FFFu, uint32_t(it - output.begin())) + 1;
                uint32_t const randOffset =
                        rw_->u32_range("offset", 0, maxOffset - 1);
                // Use either a repeated offset or the random offset
                uint32_t const offset = repeatOffset ? prevOffset : randOffset;
                // Copy the match into position
                // Overlap is allowed (length > offset)
                auto match = it - offset;
                while (it != copyEnd) {
                    *it++ = *match++;
                }
                prevOffset = offset;
            } else {
                // Generate random ASCII bytes
                auto const litEnd = std::min(it + length, end);
                while (it != litEnd) {
                    *it++ = randChar(numLiterals);
                }
            }
        }
        return output;
    }

    void print(std::ostream& os) const override
    {
        os << "CompressibleStringProducer(" << size_ << ", " << matchProb_
           << ")";
    }

   private:
    /// Generates a random readable ASCII cahracter out of `0 < numLiterals <=
    /// 95` chars
    char randChar(int numLiterals)
    {
        assert(numLiterals > 0);
        assert(numLiterals <= 95);
        if (!rw_->has_more_data()) {
            return 32;
        }
        return 32 + rw_->i8_range("num_literals", 0, numLiterals - 1);
    }

    /// Generates a random length. One in 8 is < 16 bytes, the rest < 512 bytes.
    inline uint32_t randLength()
    {
        if (rw_->u8_range("short_length", 0, 7) == 0) {
            return rw_->u8_range("short_length", 0, 15);
        } else {
            return rw_->u16_range("long_length", 0, 511);
        }
    }

    std::shared_ptr<RandWrapper> rw_;
    size_t size_;
    double matchProb_;
};

} // namespace openzl::tests::datagen
