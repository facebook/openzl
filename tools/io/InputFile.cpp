// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/io/InputFile.h"
#include "tools/io/IOException.h"

#include <cerrno>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

#include "tools/logger/Logger.h"

namespace openzl::tools::io {

using namespace logger;

InputFile::InputFile(std::string filename) : filename_(std::move(filename)) {}

poly::string_view InputFile::name() const
{
    return filename_;
}

poly::optional<size_t> InputFile::size()
{
    if (!contents_) {
        // TODO: eventually we will be stat-ing the file first and can use that
        // instead of triggering reading the contents.
        read();
    }

    return contents_.value().size();
}

poly::string_view InputFile::contents()
{
    if (!contents_) {
        read();
    }

    return contents_.value();
}

void InputFile::read()
{
    Logger::log_c(VERBOSE1, "Reading from input file '%s'", filename_.c_str());

    std::filesystem::path path(filename_);
    if (std::filesystem::exists(path) && std::filesystem::is_directory(path)) {
        throw IOException(
                "Input path '" + filename_
                + "' is a directory, but a file is required.");
    }

    static constexpr auto bits = std::ofstream::badbit | std::ofstream::failbit;
    std::ifstream in;
    in.exceptions(bits);
    try {
        in.open(filename_, std::ios::binary);
    } catch (const std::system_error&) {
        throw IOException(
                "Failed to open input file '" + filename_
                + "': " + std::system_category().message(errno));
    }

    try {
        auto ss = std::ostringstream();
        ss << in.rdbuf();
        contents_ = std::move(ss).str();
    } catch (const std::system_error& err) {
        throw IOException(
                "Failed to read input file '" + filename_
                + "': " + err.code().message());
    }
}

} // namespace openzl::tools::io
