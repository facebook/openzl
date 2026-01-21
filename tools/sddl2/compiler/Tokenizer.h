// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "tools/sddl2/compiler/Logger.h"
#include "tools/sddl2/compiler/Source.h"
#include "tools/sddl2/compiler/Token.h"

namespace openzl::sddl2 {

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

} // namespace openzl::sddl2
