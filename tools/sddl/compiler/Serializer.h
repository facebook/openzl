// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>

#include "tools/sddl/compiler/AST.h"
#include "tools/sddl/compiler/Logger.h"
#include "tools/sddl/compiler/Source.h"

namespace openzl::sddl {

/**
 * Serializes an AST to the CBOR format the SDDL graph accepts.
 */
class Serializer {
   public:
    /**
     * @param include_debug_info controls whether debugging information is
     *                           included in the emitted serialized object.
     *                           This information is not necessary for correct
     *                           execution, but helps the execution engine
     *                           produce useful error messages when execution
     *                           fails.
     */
    explicit Serializer(
            const detail::Logger& logger,
            bool include_debug_info = true);

    std::string serialize(const ASTVec& ast, const Source& source) const;

   private:
    const detail::Logger& log_;
    const bool include_debug_info_;
};

} // namespace openzl::sddl
