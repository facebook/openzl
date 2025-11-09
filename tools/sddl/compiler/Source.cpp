// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "tools/sddl/compiler/Source.h"

#include "tools/sddl/compiler/Exception.h"

namespace openzl::sddl {

Source::Source(poly::string_view contents, poly::string_view filename)
        : contents_(contents),
          filename_(filename),
          newlines_(newline_positions(contents_))
{
}

poly::string_view Source::contents() const
{
    return contents_;
}

SourceLocation Source::location(const poly::string_view str) const
{
    const auto start_ptr      = str.data();
    const auto end_ptr        = str.data() + str.size();
    const auto start_idx      = idx(start_ptr);
    const auto end_idx        = idx(end_ptr);
    const auto start_line_num = line_num_of(start_idx);
    const auto end_line_num   = line_num_of(end_idx);

    const auto start_line_start_pos = idx_of_line_start(start_line_num);
    const auto end_line_start_pos   = idx_of_line_start(end_line_num);

    const auto start_col = start_idx - start_line_start_pos;
    const auto end_col   = end_idx - end_line_start_pos;

    auto ls = lines(start_line_num, end_line_num);
    return SourceLocation(
            this,
            str,
            filename_,
            std::move(ls),
            start_line_num,
            end_line_num,
            start_col,
            end_col);
}

size_t Source::idx(const char* ptr) const
{
    if (ptr < contents_.data() || ptr > contents_.data() + contents_.size()) {
        throw InvariantViolation(
                "Source location pointer is not inside source string??");
    }
    return (size_t)(ptr - contents_.data());
}

size_t Source::line_num_of(const size_t pos) const
{
    auto it = std::lower_bound(newlines_.begin(), newlines_.end(), pos);
    return std::distance(newlines_.begin(), it) + 1;
}

size_t Source::idx_of_line_start(const size_t line_num) const
{
    if (line_num == 0) {
        throw InvariantViolation("Line number can't be 0!");
    }
    if (line_num == 1) {
        return 0;
    }
    const auto prev_nl_idx = line_num - 2;
    if (prev_nl_idx >= newlines_.size()) {
        throw InvariantViolation("Line number too large.");
    }
    return newlines_[prev_nl_idx] + 1;
}

size_t Source::idx_of_line_end(const size_t line_num) const
{
    if (line_num == newlines_.size() + 1) {
        return contents_.size();
    }
    return idx_of_line_start(line_num + 1) - 1;
}

poly::string_view Source::line(const size_t line_num) const
{
    const auto start_pos  = idx_of_line_start(line_num);
    const auto end_pos    = idx_of_line_end(line_num);
    const auto line_start = contents_.data() + start_pos;
    const auto line_end   = contents_.data() + end_pos;

    if (line_start < contents_.data()) {
        throw InvariantViolation("line_start < contents_.data()");
    }
    if (line_end < line_start) {
        throw InvariantViolation("line_end < line_start");
    }
    if (contents_.data() + contents_.size() < line_end) {
        throw InvariantViolation(
                "contents_.data() + contents_.size() < line_end");
    }

    return poly::string_view{ line_start, (size_t)(line_end - line_start) };
}

std::vector<poly::string_view> Source::lines(
        const size_t start_line,
        const size_t end_line) const
{
    std::vector<poly::string_view> ls;

    for (size_t line_num = start_line; line_num <= end_line; line_num++) {
        ls.push_back(line(line_num));
    }

    return ls;
}

std::vector<size_t> Source::newline_positions(poly::string_view src)
{
    std::vector<size_t> nls;
    for (size_t i = 0; i < src.size(); i++) {
        if (src[i] == '\n') {
            nls.push_back(i);
        }
    }
    return nls;
}

SourceLocation::SourceLocation(
        const Source* src,
        poly::string_view str,
        poly::string_view filename,
        std::vector<poly::string_view> lines,
        size_t start_line_num,
        size_t end_line_num,
        size_t start_col_num,
        size_t end_col_num)
        : src_(src),
          str_(str),
          filename_(filename),
          lines_(std::move(lines)),
          start_line_num_(start_line_num),
          end_line_num_(end_line_num),
          start_col_num_(start_col_num),
          end_col_num_(end_col_num)
{
}

SourceLocation::SourceLocation()
        : SourceLocation(nullptr, {}, {}, {}, 0, 0, 0, 0)
{
}

SourceLocation SourceLocation::null()
{
    return SourceLocation{};
}

bool SourceLocation::empty() const
{
    return src_ == nullptr;
}

size_t SourceLocation::start() const
{
    if (empty()) {
        return 0;
    }
    return src_->idx(str_.data());
}

size_t SourceLocation::size() const
{
    if (empty()) {
        return 0;
    }
    return str_.size();
}

std::string SourceLocation::pos_str() const
{
    if (empty()) {
        return "";
    }

    std::stringstream ss;
    ss << filename_ << ":" << start_line_num_;
    if (start_line_num_ != end_line_num_) {
        ss << "-" << end_line_num_;
    } else {
        ss << ":" << start_col_num_ + 1;
        if (start_col_num_ + 1 < end_col_num_) {
            ss << "-" << end_col_num_ + 1;
        }
    }

    return std::move(ss).str();
}

std::string SourceLocation::contents_str(size_t indent) const
{
    if (empty()) {
        return "";
    }

    std::stringstream ss;

    const auto end_line_num_width = std::to_string(end_line_num_).size();
    const auto num_lines          = lines_.size();

    for (size_t i = 0; i < num_lines; i++) {
        const auto& line = lines_[i];
        const auto first = i == 0;
        const auto last  = i == num_lines - 1;

        static constexpr size_t lines_to_print_at_each_end = 3;
        static constexpr size_t max_lines_to_print =
                2 * lines_to_print_at_each_end + 1;

        if (num_lines > max_lines_to_print) {
            if (i >= lines_to_print_at_each_end
                && i + lines_to_print_at_each_end < num_lines) {
                i = num_lines - lines_to_print_at_each_end - 1;
                ss << std::setw((int)indent) << ""
                   << std::setw((int)end_line_num_width + 2) << "..." << " X"
                   << '\n';
                continue;
            }
        }

        const auto tok_start_col = first ? start_col_num_ : 0;
        const auto tok_end_col   = last ? end_col_num_ : line.size();

        ss << std::setw((int)indent) << "" << "  "
           << std::setw((int)end_line_num_width) << start_line_num_ + i << " | "
           << line << '\n';
        if (tok_start_col != tok_end_col) {
            const auto highlight_len = tok_end_col - tok_start_col;
            const auto highlight_char =
                    (!first || !last || highlight_len > 1) ? '~' : '^';
            ss << std::setw((int)indent) << "" << "  "
               << std::setw((int)end_line_num_width) << "" << " | "
               << std::string(tok_start_col, ' ')
               << std::string(highlight_len, highlight_char) << '\n';
        }
    }

    return std::move(ss).str();
}

SourceLocation SourceLocation::operator+(const SourceLocation& o) const
{
    if (src_ == nullptr) {
        return o;
    }
    if (o.src_ == nullptr) {
        return *this;
    }
    if (src_ != o.src_) {
        throw InvariantViolation(
                *this,
                "Can't combine two SourceLocations from different managers!");
    }
    const auto start = std::min(str_.data(), o.str_.data());
    const auto end =
            std::max(str_.data() + str_.size(), o.str_.data() + o.str_.size());
    return src_->location(poly::string_view{ start, (size_t)(end - start) });
}

SourceLocation& SourceLocation::operator+=(const SourceLocation& o)
{
    *this = *this + o;
    return *this;
}

} // namespace openzl::sddl
