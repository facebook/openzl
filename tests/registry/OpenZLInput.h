// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "openzl/cpp/Input.hpp"

namespace openzl::tests {

/**
 * Class that holds inputs for testing OpenZL.
 * Unlike openzl::Input, this class owns the lifetime of the data.
 * Create sub-classes based on your type.
 *
 * It also supports serializing and deserializing the inputs. This
 * is mainly for supporting older format versions that don't support
 * multiple typed inputs.
 */
class OpenZLInput {
   public:
    virtual std::vector<Input> inputs() const = 0;

    std::string serialize() const;
    static std::string serialize(poly::span<const Input> inputs);
    static std::unique_ptr<OpenZLInput> deserialize(std::string_view data);

    virtual ~OpenZLInput() = default;

   private:
};

class SerialOpenZLInput : public OpenZLInput {
   public:
    explicit SerialOpenZLInput(std::string str) : str_(std::move(str)) {}

    template <typename... Args>
    static std::unique_ptr<OpenZLInput> make(Args&&... args)
    {
        return std::make_unique<SerialOpenZLInput>(std::forward<Args>(args)...);
    }

    std::vector<Input> inputs() const override
    {
        std::vector<Input> result;
        result.push_back(Input::refSerial(str_));
        return result;
    }

   private:
    std::string str_;
};

template <typename T>
class NumericOpenZLInput : public OpenZLInput {
    static_assert(std::is_arithmetic<T>{}, "Must be arithmetic");

   public:
    explicit NumericOpenZLInput(std::vector<T> vec) : vec_(std::move(vec))
    {
        if (vec_.empty()) {
            vec_.reserve(1);
        }
    }
    explicit NumericOpenZLInput(std::string str)
    {
        if (str.size() % sizeof(T) != 0) {
            throw std::runtime_error(
                    "Input length must be a multiple of eltWidth");
        }
        vec_.reserve(std::max<size_t>(str.size() / sizeof(T), 1));
        for (size_t i = 0; i < str.size(); i += sizeof(T)) {
            T value;
            memcpy(&value, str.data() + i, sizeof(T));
            vec_.push_back(value);
        }
    }

    template <typename... Args>
    static std::unique_ptr<OpenZLInput> make(Args&&... args)
    {
        return std::make_unique<NumericOpenZLInput>(
                std::forward<Args>(args)...);
    }

    std::vector<Input> inputs() const override
    {
        std::vector<Input> result;
        result.push_back(Input::refNumeric(poly::span<const T>(vec_)));
        return result;
    }

    ~NumericOpenZLInput() = default;

   private:
    std::vector<T> vec_;
};

using U8OpenZLInput  = NumericOpenZLInput<uint8_t>;
using U16OpenZLInput = NumericOpenZLInput<uint16_t>;
using U32OpenZLInput = NumericOpenZLInput<uint32_t>;
using U64OpenZLInput = NumericOpenZLInput<uint64_t>;

inline std::unique_ptr<OpenZLInput> makeNumericInput(
        std::string str,
        size_t eltWidth)
{
    switch (eltWidth) {
        case 1:
            return U8OpenZLInput::make(str);
        case 2:
            return U16OpenZLInput::make(str);
        case 4:
            return U32OpenZLInput::make(str);
        case 8:
            return U64OpenZLInput::make(str);
        default:
            throw std::runtime_error("Unsupported eltWidth");
    }
}

class StructOpenZLInput : public OpenZLInput {
   public:
    StructOpenZLInput(std::string data, size_t eltWidth)
            : data_(std::move(data)), width_(eltWidth)
    {
    }

    template <typename... Args>
    static std::unique_ptr<OpenZLInput> make(Args&&... args)
    {
        return std::make_unique<StructOpenZLInput>(std::forward<Args>(args)...);
    }

    std::vector<Input> inputs() const
    {
        std::vector<Input> result;
        result.push_back(
                Input::refStruct(data_.data(), width_, data_.size() / width_));
        return result;
    }

    ~StructOpenZLInput() = default;

   private:
    std::string data_;
    size_t width_;
};

class StringOpenZLInput : public OpenZLInput {
   public:
    StringOpenZLInput(std::string data, std::vector<uint32_t> lens)
            : data_(std::move(data)), lens_(std::move(lens))
    {
        if (lens_.empty()) {
            lens_.reserve(1); // force non-null pointer
        }
    }

    explicit StringOpenZLInput(poly::span<const std::string> strs)
    {
        lens_.reserve(std::max<size_t>(strs.size(), 1));
        for (const auto& str : strs) {
            data_ += str;
            lens_.push_back(str.size());
        }
    }

    template <typename... Args>
    static std::unique_ptr<OpenZLInput> make(Args&&... args)
    {
        return std::make_unique<StringOpenZLInput>(std::forward<Args>(args)...);
    }

    std::vector<Input> inputs() const
    {
        std::vector<Input> result;
        result.push_back(Input::refString(data_, lens_));
        return result;
    }

    ~StringOpenZLInput() = default;

   private:
    std::string data_;
    std::vector<uint32_t> lens_;
};

class MultiOpenZLInput : public OpenZLInput {
   public:
    explicit MultiOpenZLInput(std::vector<std::unique_ptr<OpenZLInput>> inputs)
            : inputs_(std::move(inputs))

    {
    }

    template <typename... Args>
    static std::unique_ptr<OpenZLInput> make(Args&&... args)
    {
        return std::make_unique<MultiOpenZLInput>(std::forward<Args>(args)...);
    }

    std::vector<Input> inputs() const override
    {
        std::vector<Input> ins;
        ins.reserve(inputs_.size());
        for (const auto& input : inputs_) {
            auto inner = input->inputs();
            if (inner.size() != 1) {
                throw std::runtime_error("MultiOpenZLInput cannot be nested");
            }
            ins.push_back(std::move(inner[0]));
        }
        return ins;
    }

   private:
    std::vector<std::unique_ptr<OpenZLInput>> inputs_;
};

} // namespace openzl::tests
