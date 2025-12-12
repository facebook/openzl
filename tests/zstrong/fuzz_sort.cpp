// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <algorithm>

#include "tests/datagen/DataGen.h"
#include "tests/fuzz_utils.h"

#include "openzl/shared/pdqsort.h"

namespace openzl {
namespace tests {
namespace {

template <typename T>
void fuzzPdqsort_inner(datagen::DataGen& dg)
{
    size_t const eltWidth = sizeof(T);
    std::vector<T> input  = dg.randVector<T>(
            "input_data", T(0), std::numeric_limits<T>::max(), 1000);
    std::vector<T> verification = input;
    ASSERT_EQ(input, verification);
    pdqsort(input.data(), input.size(), eltWidth);
    // sort the verification vector
    std::sort(verification.begin(), verification.end());
    ASSERT_EQ(input, verification);
}

FUZZ(SortTest, FuzzPDQsort)
{
    datagen::DataGen dg = fromFDP(f);
    size_t const eltWidth =
            dg.choices("elt_width", std::vector<size_t>{ 1, 2, 4, 8 });
    switch (eltWidth) {
        case 1:
            fuzzPdqsort_inner<uint8_t>(dg);
            break;
        case 2:
            fuzzPdqsort_inner<uint16_t>(dg);
            break;
        case 4:
            fuzzPdqsort_inner<uint32_t>(dg);
            break;
        case 8:
            fuzzPdqsort_inner<uint64_t>(dg);
            break;
    }
}

} // namespace
} // namespace tests
} // namespace openzl
