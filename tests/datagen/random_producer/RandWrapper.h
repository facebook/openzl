// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <vector>

namespace zstrong::tests::datagen {

class RandWrapper {
   public:
    enum class RandType {
        MT19937,
        StructuredFDP,
    };

    // Lionhead requires that we pass a name parameter every time we ask for
    // data. However, actually constructing a string and shuttling it around is
    // costly and not used during CI fuzzing. The way they get around this is by
    // defining a `NameType` for each mode. For `ModePrint` it is
    // folly::StringPiece, but `ModeFuzz` it is a class ModeUsesNoNames that
    // simply no-ops the entire thing. We attempt something similar here using
    // string_view. For normal operation, no name is needed, and a smart
    // compiler can make it (almost) a no-op. For fuzzing, we do some delegation
    // to the underlying `NameType` via implicit conversion.
    using NameType = const char*;

    // these are semi-copied from fdp_impl.h
    virtual uint8_t u8(NameType name)   = 0;
    virtual uint32_t u32(NameType name) = 0;
    virtual uint64_t u64(NameType name) = 0;
    virtual float f32(NameType name)    = 0;
    virtual double f64(NameType name)   = 0;

    virtual size_t usize_range(NameType name, size_t min, size_t max)     = 0;
    virtual uint8_t u8_range(NameType name, uint8_t min, uint8_t max)     = 0;
    virtual uint16_t u16_range(NameType name, uint16_t min, uint16_t max) = 0;
    virtual uint32_t u32_range(NameType name, uint32_t min, uint32_t max) = 0;
    virtual uint64_t u64_range(NameType name, uint64_t min, uint64_t max) = 0;
    virtual int8_t i8_range(NameType name, int8_t min, int8_t max)        = 0;
    virtual int16_t i16_range(NameType name, int16_t min, int16_t max)    = 0;
    virtual int32_t i32_range(NameType name, int32_t min, int32_t max)    = 0;
    virtual int64_t i64_range(NameType name, int64_t min, int64_t max)    = 0;
    virtual float f32_range(NameType name, float min, float max)          = 0;
    virtual double f64_range(NameType name, double min, double max)       = 0;

    // only applicable to fuzzers
    virtual bool has_more_data()                       = 0;
    virtual std::vector<uint8_t> all_remaining_bytes() = 0;

    virtual bool boolean(NameType name)
    {
        return (bool)range(name, (uint8_t)0, (uint8_t)1);
    }

    // The following template nonsense provides:
    // ```
    // template <typename T>
    // T range(NameType name, T min, T min);
    // ```
    // for all of the above types. Sorry.

    template <typename T, typename U>
    typename std::enable_if<
            std::is_integral<T>::value && sizeof(T) == sizeof(uint8_t)
                    && !std::is_signed<T>::value && std::is_integral<U>::value
                    && sizeof(U) == sizeof(uint8_t)
                    && !std::is_signed<U>::value,
            decltype(std::declval<T>() + std::declval<U>())>::type
    range(NameType name, T min, U max)
    {
        return u8_range(name, min, max);
    }

    template <typename T, typename U>
    typename std::enable_if<
            std::is_integral<T>::value && sizeof(T) == sizeof(uint16_t)
                    && !std::is_signed<T>::value && std::is_integral<U>::value
                    && sizeof(U) == sizeof(uint16_t)
                    && !std::is_signed<U>::value,
            decltype(std::declval<T>() + std::declval<U>())>::type
    range(NameType name, T min, U max)
    {
        return u16_range(name, min, max);
    }

    template <typename T, typename U>
    typename std::enable_if<
            std::is_integral<T>::value && sizeof(T) == sizeof(uint32_t)
                    && !std::is_signed<T>::value && std::is_integral<U>::value
                    && sizeof(U) == sizeof(uint32_t)
                    && !std::is_signed<U>::value,
            decltype(std::declval<T>() + std::declval<U>())>::type
    range(NameType name, T min, U max)
    {
        return u32_range(name, min, max);
    }

    template <typename T, typename U>
    typename std::enable_if<
            std::is_integral<T>::value && sizeof(T) == sizeof(uint64_t)
                    && !std::is_signed<T>::value && std::is_integral<U>::value
                    && sizeof(U) == sizeof(uint64_t)
                    && !std::is_signed<U>::value,
            decltype(std::declval<T>() + std::declval<U>())>::type
    range(NameType name, T min, U max)
    {
        return u64_range(name, min, max);
    }

    template <typename T, typename U>
    typename std::enable_if<
            std::is_integral<T>::value && sizeof(T) == sizeof(int8_t)
                    && std::is_signed<T>::value && std::is_integral<U>::value
                    && sizeof(U) == sizeof(int8_t) && std::is_signed<U>::value,
            decltype(std::declval<T>() + std::declval<U>())>::type
    range(NameType name, T min, U max)
    {
        return i8_range(name, min, max);
    }

    template <typename T, typename U>
    typename std::enable_if<
            std::is_integral<T>::value && sizeof(T) == sizeof(int16_t)
                    && std::is_signed<T>::value && std::is_integral<U>::value
                    && sizeof(U) == sizeof(int16_t) && std::is_signed<U>::value,
            decltype(std::declval<T>() + std::declval<U>())>::type
    range(NameType name, T min, U max)
    {
        return i16_range(name, min, max);
    }

    template <typename T, typename U>
    typename std::enable_if<
            std::is_integral<T>::value && sizeof(T) == sizeof(int32_t)
                    && std::is_signed<T>::value && std::is_integral<U>::value
                    && sizeof(U) == sizeof(int32_t) && std::is_signed<U>::value,
            decltype(std::declval<T>() + std::declval<U>())>::type
    range(NameType name, T min, U max)
    {
        return i32_range(name, min, max);
    }

    template <typename T, typename U>
    typename std::enable_if<
            std::is_integral<T>::value && sizeof(T) == sizeof(int64_t)
                    && std::is_signed<T>::value && std::is_integral<U>::value
                    && sizeof(U) == sizeof(int64_t) && std::is_signed<U>::value,
            decltype(std::declval<T>() + std::declval<U>())>::type
    range(NameType name, T min, U max)
    {
        return i64_range(name, min, max);
    }

    template <typename T>
    typename std::enable_if<std::is_same<T, float>::value, T>::type
    range(NameType name, T min, T max)
    {
        return f32_range(name, min, max);
    }

    template <typename T>
    typename std::enable_if<std::is_same<T, double>::value, T>::type
    range(NameType name, T min, T max)
    {
        return f64_range(name, min, max);
    }

    virtual ~RandWrapper()                     = default;
    RandWrapper(RandWrapper&&)                 = delete;
    RandWrapper& operator=(RandWrapper&&)      = delete;
    RandWrapper(const RandWrapper&)            = delete;
    RandWrapper& operator=(const RandWrapper&) = delete;

    const RandType type;

   protected:
    explicit RandWrapper(RandType type_) : type(type_) {}
};

} // namespace zstrong::tests::datagen
