// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tests/zstrong/test_multi_input_fixture.h"

namespace openzl {
namespace tests {
std::unique_ptr<ZL_TypedRef, ZS2_TypedRef_Deleter>
MultiInputTest::getTypedInput(TypedInputDesc const& inputDesc)
{
    ZL_TypedRef* tref;
    switch (inputDesc.type) {
        case ZL_Type_serial:
            tref = ZL_TypedRef_createSerial(
                    inputDesc.data.data(), inputDesc.data.size());
            break;
        case ZL_Type_struct:
            tref = ZL_TypedRef_createStruct(
                    inputDesc.data.data(),
                    inputDesc.eltWidth,
                    inputDesc.data.size() / inputDesc.eltWidth);
            break;
        case ZL_Type_numeric:
            tref = ZL_TypedRef_createNumeric(
                    inputDesc.data.data(),
                    inputDesc.eltWidth,
                    inputDesc.data.size() / inputDesc.eltWidth);
            break;
        case ZL_Type_string:
            tref = ZL_TypedRef_createString(
                    inputDesc.data.data(),
                    inputDesc.data.size(),
                    inputDesc.strLens.data(),
                    inputDesc.strLens.size());
            break;
        default:
            throw std::runtime_error("Unknown type provided");
    }
    return std::unique_ptr<ZL_TypedRef, ZS2_TypedRef_Deleter>(tref);
}
} // namespace tests
} // namespace openzl
