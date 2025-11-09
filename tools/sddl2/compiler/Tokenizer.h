// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>

#include "openzl/cpp/poly/StringView.hpp"

#include "tools/sddl/compiler/Logger.h"
#include "tools/sddl/compiler/Source.h"
#include "tools/sddl/compiler/Token.h"

namespace openzl::sddl {

/**
 * Takes source code and converts it into a flat array of tokens.
 */
class Tokenizer {
   public:
    explicit Tokenizer(const detail::Logger& logger);

    std::vector<Token> tokenize(const Source& source) const;

   private:
    const detail::Logger& log_;
};

} // namespace openzl::sddl
