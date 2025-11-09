// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl/compiler/Logger.h"

#include <fstream>

namespace openzl::sddl::detail {

namespace {

std::ofstream null_ofstream{ []() {
    std::ofstream ofs;
    ofs.setstate(std::ios_base::badbit);
    return ofs;
}() };

} // anonymous namespace

Logger::Logger(std::ostream& os, int verbosity)
        : os_(os), null_(null_ofstream), verbosity_(verbosity)
{
}

std::ostream& Logger::operator()(int level) const
{
    if (level <= verbosity_) {
        return os_;
    } else {
        return null_;
    }
}

} // namespace openzl::sddl::detail
