// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>

namespace openzl::arg {

enum class NumArgs {
    ZeroOrOne,
    One,
    ZeroOrMore,
    OneOrMore,
};

struct Positional {
    NumArgs numArgs;
    std::string name;
    std::string help;
};

} // namespace openzl::arg
