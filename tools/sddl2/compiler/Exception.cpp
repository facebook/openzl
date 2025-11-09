// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl/compiler/Exception.h"

#include <sstream>

namespace openzl::sddl {

namespace {

std::string make_msg(
        const SourceLocation& loc,
        const poly::string_view& error_type,
        const poly::string_view& msg)
{
    if (loc.empty()) {
        return std::string{ msg } + '\n';
    }
    std::stringstream ss;
    ss << loc.pos_str() << ": " << error_type << ": " << msg << "\n";
    ss << loc.contents_str();
    return std::move(ss).str();
}

} // anonymous namespace

CompilerException::CompilerException(
        const SourceLocation& loc,
        const poly::string_view& error_type,
        const poly::string_view& msg)
        : runtime_error(msg.data()),
          loc_(loc),
          msg_(make_msg(loc, error_type, msg))
{
}

const SourceLocation& CompilerException::loc() const noexcept
{
    return loc_;
}

const char* CompilerException::what() const noexcept
{
    return msg_.c_str();
}

} // namespace openzl::sddl
