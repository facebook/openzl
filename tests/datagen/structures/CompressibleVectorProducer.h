// (c) Meta Platforms, Inc. and affiliates. Confidential and proprietary.

#pragma once

#include <limits>
#include <type_traits>

#include "tests/datagen/DataProducer.h"

namespace openzl::tests::datagen {

template <typename T>
class CompressibleVectorProducer : public DataProducer<std::vector<T>> {
    static_assert(std::is_integral<T>::value, "T must be an integral type");

   public:
    explicit CompressibleVectorProducer(
            std::shared_ptr<RandWrapper> rw,
            size_t numElements,
            double matchProb = 0.5)
            : DataProducer<std::vector<T>>(),
              rw_(rw),
              numElements_(numElements),
              matchProb_(matchProb)
    {
        if (matchProb < 0.0 || matchProb > 1.0) {
            throw std::invalid_argument(
                    "matchProb must be between 0.0 and 1.0");
        }
    }

    std::vector<T> operator()(RandWrapper::NameType name) override
    {
        int const matchProb = int(matchProb_ * 100);
        std::vector<T> output;
        output.resize(numElements_);
        if (output.empty()) {
            return output;
        }
        // Generate the first element so we can potentially match it
        auto it = output.begin();
        *it++   = randElement();

        uint32_t prevOffset = 1;
        for (auto const end = output.end(); it != end;) {
            if (!rw_->has_more_data()) {
                break;
            }
            // Pick the segment length (in elements)
            uint32_t const length = randLength();
            // Either a match or literals
            if (rw_->u8_range("is_match", 0, 99) < matchProb) {
                // copy up to length elements
                auto const copyEnd = std::min(it + length, end);
                // Use a previous offset 1/16 of the time
                bool const repeatOffset =
                        (rw_->u8_range("repeat_offset", 0, 15) == 0);
                // Pick a random offset <= 32K elements away
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
                // Generate random elements
                auto const litEnd = std::min(it + length, end);
                while (it != litEnd) {
                    *it++ = randElement();
                }
            }
        }
        return output;
    }

    void print(std::ostream& os) const override
    {
        os << "CompressibleVectorProducer(" << numElements_ << ", "
           << matchProb_ << ")";
    }

   private:
    /// Generates a random element from an alphabet of size numLiterals
    T randElement()
    {
        if (!rw_->has_more_data()) {
            return 0;
        }
        return rw_->range<T>(
                "literal",
                std::numeric_limits<T>::min(),
                std::numeric_limits<T>::max());
    }

    /// Generates a random length. One in 8 is < 16 elements, the rest < 512.
    inline uint32_t randLength()
    {
        if (rw_->u8_range("short_length", 0, 7) == 0) {
            return rw_->u8_range("short_length", 0, 15);
        } else {
            return rw_->u16_range("long_length", 0, 511);
        }
    }

    std::shared_ptr<RandWrapper> rw_;
    size_t numElements_;
    double matchProb_;
};

} // namespace openzl::tests::datagen
