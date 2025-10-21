// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once
#include <string>
#include <vector>
#include "tools/protobuf/mem.h"

namespace openzl {
namespace protobuf {

class StringWriter {
   public:
    StringWriter() = default;
    explicit StringWriter(size_t len) : initLen_(len) {}
    ~StringWriter() = default;

    /**
     * Write a value of type T to the string, in little-endian format.
     */
    template <typename T>
    void writeLE(const T& val)
    {
        static_assert(
                std::is_integral<T>::value || std::is_floating_point<T>::value);

        ensure(sizeof(T));
        if constexpr (std::is_same_v<T, bool>) {
            *ptr() = val ? 1 : 0;
        } else {
            T le = utils::toLE(val);
            memcpy(ptr(), &le, sizeof(T));
        }
        pos_ += sizeof(T);
    }

    void write(const std::string& val);

    /**
     * Get the final string from the writer. The writer is then reset to an
     * empty state.
     */
    std::string move();

   private:
    std::vector<std::string> bufs_;
    size_t initLen_ = 1024;
    size_t idx_     = 0;
    size_t pos_     = 0;

    /**
     * Initializes the writer and resets the current
     * writer state if any.
     */
    void init();

    /**
     * Returns a pointer to the current position in the current buffer.
     */
    char* ptr();

    /**
     * Returns the number of bytes remaining in the current buffer.
     */
    size_t remaining() const;

    /**
     * Adds a new buffer to the list. The new buffer is twice the size of the
     * previous one.
     */
    void grow();

    /**
     * Ensures that there is enough space for the given number of contiguous
     * bytes.
     */
    void ensure(size_t len);
};

} // namespace protobuf
} // namespace openzl
