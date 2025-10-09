// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once
#include <gtest/gtest.h>

#include "tests/zstrong/test_zstrong_fixture.h"

namespace zstrong {
namespace tests {
class MultiInputTest : public ZStrongTest {
   public:
    std::unique_ptr<ZL_TypedRef, ZL_TypedRef_Deleter> getTypedInput(
            TypedInputDesc const& inputDesc);
};
} // namespace tests
} // namespace zstrong
