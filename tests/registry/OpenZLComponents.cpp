// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tests/registry/OpenZLComponents.h"
#include "openzl/cpp/poly/Span.hpp"

namespace openzl::tests {

poly::span<const std::unique_ptr<OpenZLComponent>> getAllOpenZLComponents()
{
    static auto* componentsPtr =
            new std::vector<std::unique_ptr<OpenZLComponent>>{ [] {
                std::vector<std::unique_ptr<OpenZLComponent>> components;
                components.reserve(int(OpenZLComponentID::NumComponents));
                for (int component = 0;
                     component < int(OpenZLComponentID::NumComponents);
                     ++component) {
                    components.push_back(
                            makeOpenZLComponent(OpenZLComponentID(component)));
                }
                return components;
            }() };
    return *componentsPtr;
}

} // namespace openzl::tests
