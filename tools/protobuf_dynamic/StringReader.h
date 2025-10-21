// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once
#include <stdexcept>
#include <string>
#include "tools/protobuf/mem.h"

namespace openzl {
namespace protobuf {

class StringReader {
   public:
    explicit StringReader(const std::string_view& str) : str_(str) {}

    /**
     * Read a value of type T from the string, in little-endian format.
     */
    template <typename T>
    void readLE(T& val)
    {
        static_assert(
                std::is_integral<T>::value || std::is_floating_point<T>::value);
        check(sizeof(T));
        memcpy(&val, str_.data() + pos_, sizeof(T));
        val = utils::toLE(val);
        pos_ += sizeof(T);
    }

    /**
     * Copy a string of length len from the string into val.
     */
    void read(std::string& val, size_t len)
    {
        check(len);
        val.resize(len);
        memcpy(val.data(), str_.data() + pos_, len);
        pos_ += len;
    }

    /**
     * Return true if the string has been fully read.
     */
    bool atEnd() const
    {
        return pos_ == str_.size();
    }

   private:
    const std::string_view str_;
    size_t pos_ = 0;

    void check(size_t len) const
    {
        if (pos_ + len > str_.size()) {
            throw std::out_of_range("StringReader: out of bounds");
        }
    }
};

} // namespace protobuf
} // namespace openzl
