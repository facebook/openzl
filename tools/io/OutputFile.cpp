// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/io/OutputFile.h"
#include "tools/io/IOException.h"
#include "tools/logger/Logger.h"

#include <cerrno>
#include <fstream>
#include <system_error>

namespace openzl::tools::io {

using namespace openzl::tools::logger;

OutputFile::OutputFile(std::string filename) : filename_(std::move(filename)) {}

poly::string_view OutputFile::name() const
{
    return filename_;
}

void OutputFile::open()
{
    static constexpr auto bits = std::ofstream::badbit | std::ofstream::failbit
            | std::ofstream::eofbit;
    os_.emplace();
    os_->exceptions(bits);
    try {
        os_->open(filename_, std::ios::binary);
    } catch (const std::system_error&) {
        os_.reset();
        throw IOException(
                "Failed to open output file '" + filename_
                + "': " + std::system_category().message(errno));
    }
}

void OutputFile::close()
{
    try {
        os_.reset();
    } catch (const std::system_error&) {
        throw IOException(
                "Failed to close output file '" + filename_
                + "': " + std::system_category().message(errno));
    }
}

void OutputFile::write(poly::string_view contents)
{
    Logger::log_c(VERBOSE1, "Writing to output file '%s'", filename_.c_str());
    if (!os_) {
        open();
    }

    try {
        os_->write(contents.data(), contents.size());
    } catch (const std::system_error& err) {
        // It's not clear that errno is set during failed write() calls.
        // But err.code().message() sucks.
        throw IOException(
                "Failed to write to output file '" + filename_
                + "': " + err.code().message());
    }
}

std::ostream& OutputFile::get_ostream()
{
    return os_.value();
}

} // namespace openzl::tools::io
