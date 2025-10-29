// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once
#include <google/protobuf/reflection.h>
#include <cstdint>
#include <string>
#include "openzl/zl_data.h"

namespace openzl {
namespace protobuf {
using Reflection      = google::protobuf::Reflection;
using Message         = google::protobuf::Message;
using FieldDescriptor = google::protobuf::FieldDescriptor;
using CPPType         = google::protobuf::FieldDescriptor::CppType;

/**
 * The type of the input that is passed to OpenZL.
 */
enum class InputType {
    INVALID      = -1,
    FIELD_ID     = 0,
    FIELD_TYPE   = 1,
    FIELD_LENGTH = 2,
    INT32        = 3,
    INT64        = 4,
    UINT32       = 5,
    UINT64       = 6,
    FLOAT        = 7,
    DOUBLE       = 8,
    BOOL         = 9,
    ENUM         = 10,
    STRING       = 11,
    MAX          = 11 // keep last
};
constexpr size_t kNumInputs = (int)InputType::MAX + 1;

/**
 * Maps from the C++ type to the InputType enum.
 */
constexpr std::array<InputType, CPPType::MAX_CPPTYPE + 1> CPPTypeToInputType = {
    InputType::INVALID, InputType::INT32,  InputType::INT64,  InputType::UINT32,
    InputType::UINT64,  InputType::DOUBLE, InputType::FLOAT,  InputType::BOOL,
    InputType::ENUM,    InputType::STRING, InputType::INVALID
};
constexpr uint32_t kStop = 0;

/**
 * Helper that calls a given function with the correct template parameter based
 * on the input type.
 *
 * @param t The input type
 * @param f The function to call
 * @param args The arguments to pass to the function
 */
template <typename Func, typename... Args>
auto call(const InputType& t, Func&& f, Args&&... args)
{
    switch (t) {
        case InputType::FIELD_ID:
            return f.template operator()<InputType::FIELD_ID>(
                    std::forward<Args>(args)...);
            break;
        case InputType::FIELD_TYPE:
            return f.template operator()<InputType::FIELD_TYPE>(
                    std::forward<Args>(args)...);
            break;
        case InputType::FIELD_LENGTH:
            return f.template operator()<InputType::FIELD_LENGTH>(
                    std::forward<Args>(args)...);
            break;
        case InputType::INT32:
            return f.template operator()<InputType::INT32>(
                    std::forward<Args>(args)...);
            break;
        case InputType::INT64:
            return f.template operator()<InputType::INT64>(
                    std::forward<Args>(args)...);
            break;
        case InputType::UINT32:
            return f.template operator()<InputType::UINT32>(
                    std::forward<Args>(args)...);
            break;
        case InputType::UINT64:
            return f.template operator()<InputType::UINT64>(
                    std::forward<Args>(args)...);
            break;
        case InputType::FLOAT:
            return f.template operator()<InputType::FLOAT>(
                    std::forward<Args>(args)...);
            break;
        case InputType::DOUBLE:
            return f.template operator()<InputType::DOUBLE>(
                    std::forward<Args>(args)...);
            break;
        case InputType::BOOL:
            return f.template operator()<InputType::BOOL>(
                    std::forward<Args>(args)...);
            break;
        case InputType::ENUM:
            return f.template operator()<InputType::ENUM>(
                    std::forward<Args>(args)...);
            break;
        case InputType::STRING:
            return f.template operator()<InputType::STRING>(
                    std::forward<Args>(args)...);
            break;
        case InputType::INVALID:
        default:
            throw std::invalid_argument("Unknown InputType");
    }
}

template <typename T>
using GetFn = T (Reflection::*)(const Message&, const FieldDescriptor*) const;
template <typename T>
using SetFn = void (Reflection::*)(Message*, const FieldDescriptor*, T) const;

template <InputType Type>
struct InputTraits {
    using type                          = uint32_t;
    static constexpr ZL_Type zl_type    = ZL_Type_numeric;
    static constexpr uint16_t elm_width = 4;
    static constexpr GetFn<type> Get    = nullptr;
    static constexpr SetFn<type> Set    = nullptr;
};

template <>
struct InputTraits<InputType::INT32> {
    using type                          = int32_t;
    static constexpr ZL_Type zl_type    = ZL_Type_numeric;
    static constexpr uint16_t elm_width = 4;
    static constexpr GetFn<type> Get    = &Reflection::GetInt32;
    static constexpr SetFn<type> Set    = &Reflection::SetInt32;
};

template <>
struct InputTraits<InputType::INT64> {
    using type                          = int64_t;
    static constexpr ZL_Type zl_type    = ZL_Type_numeric;
    static constexpr uint16_t elm_width = 8;
    static constexpr GetFn<type> Get    = &Reflection::GetInt64;
    static constexpr SetFn<type> Set    = &Reflection::SetInt64;
};

template <>
struct InputTraits<InputType::UINT32> {
    using type                          = uint32_t;
    static constexpr ZL_Type zl_type    = ZL_Type_numeric;
    static constexpr uint16_t elm_width = 4;
    static constexpr GetFn<type> Get    = &Reflection::GetUInt32;
    static constexpr SetFn<type> Set    = &Reflection::SetUInt32;
};

template <>
struct InputTraits<InputType::UINT64> {
    using type                          = uint64_t;
    static constexpr ZL_Type zl_type    = ZL_Type_numeric;
    static constexpr uint16_t elm_width = 8;
    static constexpr GetFn<type> Get    = &Reflection::GetUInt64;
    static constexpr SetFn<type> Set    = &Reflection::SetUInt64;
};

template <>
struct InputTraits<InputType::FLOAT> {
    using type                          = float;
    static constexpr ZL_Type zl_type    = ZL_Type_numeric;
    static constexpr uint16_t elm_width = 4;
    static constexpr GetFn<type> Get    = &Reflection::GetFloat;
    static constexpr SetFn<type> Set    = &Reflection::SetFloat;
};

template <>
struct InputTraits<InputType::DOUBLE> {
    using type                          = double;
    static constexpr ZL_Type zl_type    = ZL_Type_numeric;
    static constexpr uint16_t elm_width = 8;
    static constexpr GetFn<type> Get    = &Reflection::GetDouble;
    static constexpr SetFn<type> Set    = &Reflection::SetDouble;
};

template <>
struct InputTraits<InputType::BOOL> {
    using type                          = bool;
    static constexpr ZL_Type zl_type    = ZL_Type_numeric;
    static constexpr uint16_t elm_width = 1;
    static constexpr GetFn<type> Get    = &Reflection::GetBool;
    static constexpr SetFn<type> Set    = &Reflection::SetBool;
};

template <>
struct InputTraits<InputType::ENUM> {
    using type                          = int32_t;
    static constexpr ZL_Type zl_type    = ZL_Type_numeric;
    static constexpr uint16_t elm_width = 4;
    static constexpr GetFn<type> Get    = &Reflection::GetEnumValue;
    static constexpr SetFn<type> Set    = &Reflection::SetEnumValue;
};

template <>
struct InputTraits<InputType::STRING> {
    using type                          = std::string;
    static constexpr ZL_Type zl_type    = ZL_Type_serial;
    static constexpr uint16_t elm_width = 1;
    static constexpr GetFn<type> Get    = &Reflection::GetString;
    static constexpr SetFn<type> Set    = &Reflection::SetString;
};
} // namespace protobuf
} // namespace openzl
