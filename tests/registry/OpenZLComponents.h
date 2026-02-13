// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tests/registry/OpenZLComponent.h"

/**
 * To add a new component:
 * 1. Add a new enum value to OpenZLComponentID (call it ${COMPONENT})
 * 2. Add your function declaration to the components namespace called
 *    make${COMPONENT}Component()
 * 3. Add your component to the switch statement in makeComponent().
 * 4. Add your function definition to components/. Prefer to put it
 *    in components/${COMPONENT}.cpp, unless the implementation is shared
 *    with a family of components.
 *
 * Notes:
 * - Components do not need to map 1:1 to OpenZL codecs, nodes, or graphs.
 *   A single codec, node, or graph could have multiple components if it is
 *   used in multiple disparate ways.
 * - All OpenZL codecs, nodes, and graphs should be registered, otherwise they
 *   will not be covered by OpenZL's full test and fuzzer suite.
 */

namespace openzl::tests {

enum class OpenZLComponentID {
    Zstd,
    // Must be last enum value
    NumComponents,
};

/**
 * @returns The component for the ID
 */
inline std::unique_ptr<OpenZLComponent> makeComponent(
        OpenZLComponentID component);

namespace components {
std::unique_ptr<OpenZLComponent> makeZstdComponent();
}

inline std::unique_ptr<OpenZLComponent> makeOpenZLComponent(
        OpenZLComponentID component)
{
    switch (component) {
        case OpenZLComponentID::Zstd:
            return components::makeZstdComponent();
        case OpenZLComponentID::NumComponents:
        default:
            throw std::runtime_error("Invalid component");
    }
}

poly::span<const std::unique_ptr<OpenZLComponent>> getAllOpenZLComponents();

} // namespace openzl::tests
