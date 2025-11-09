// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "openzl/cpp/poly/StringView.hpp"

namespace openzl::sddl {

// Forward declaration, declared fully below.
class SourceLocation;

/**
 * Represents an input buffer / file to be compiled.
 *
 * Primarily exists to serve as a manager for creating SourceLocations for
 * substrings of the source.
 */
class Source {
    friend class SourceLocation;

   public:
    /**
     * The buffers backing the string_views passed in must outlive the Source
     * and also any SourceLocations generated from this Source. Additionally,
     * the Source itself must outlive any SourceLocations it produces.
     */
    Source(poly::string_view contents, poly::string_view filename);

    Source(const Source&) = delete;
    Source(Source&&)      = delete;

    Source& operator=(const Source&) = delete;
    Source& operator=(Source&&)      = delete;

    poly::string_view contents() const;

    /**
     * Make a location representing @p str in this input buffer. @p src must
     * point inside the same buffer originally passed in.
     */
    SourceLocation location(const poly::string_view str) const;

   private:
    size_t idx(const char* ptr) const;

    size_t line_num_of(const size_t pos) const;

    size_t idx_of_line_start(const size_t line_num) const;

    size_t idx_of_line_end(const size_t line_num) const;

    poly::string_view line(const size_t line_num) const;

    std::vector<poly::string_view> lines(
            const size_t start_line,
            const size_t end_line) const;

    static std::vector<size_t> newline_positions(poly::string_view src);

   private:
    const poly::string_view contents_;
    const poly::string_view filename_;
    const std::vector<size_t> newlines_;
};

/**
 * Represents a location range in the source code. Used throughout the compiler
 * so that objects can keep track of the source code they came from, so that
 * errors or debug messages about an object can print a useful identification
 * of the relevant part of the source code.
 */
class SourceLocation {
   private:
    friend class Source;

   protected:
    SourceLocation(
            const Source* source,
            poly::string_view str,
            poly::string_view filename,
            std::vector<poly::string_view> lines,
            size_t start_line_num,
            size_t end_line_num,
            size_t start_col_num,
            size_t end_col_num);

    /**
     * Constructs an empty location that points nowhere.
     */
    SourceLocation();

   public:
    static SourceLocation null();

    bool empty() const;

    size_t start() const;

    size_t size() const;

    /**
     * @returns a string like "file.sddl:1234:10-20" if non-empty, otherwise "".
     */
    std::string pos_str() const;

    /**
     * @returns a string like:
     *
     * ```
     *   123 | some_source_code = that() * you + wrote;
     *       |                    ~~~~~~
     * ```
     *
     * if non-empty, "" otherwise.
     */
    std::string contents_str(size_t indent = 0) const;

    /**
     * Join two locations into one, including any content between the two.
     * @p o must be from the same manager as this one.
     */
    SourceLocation operator+(const SourceLocation& o) const;

    /**
     * `*this = *this + o`
     */
    SourceLocation& operator+=(const SourceLocation& o);

   private:
    const Source* src_;
    poly::string_view str_;
    poly::string_view filename_;
    std::vector<poly::string_view> lines_;
    size_t start_line_num_;
    size_t end_line_num_;
    size_t start_col_num_;
    size_t end_col_num_;
};

} // namespace openzl::sddl
