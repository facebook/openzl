// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <arrow/api.h>
#include <parquet/exception.h>
#include <string>
#include <vector>

namespace zstrong {
namespace parquet {
namespace testing {

std::string to_canonical_parquet(
        const std::shared_ptr<arrow::Table> table,
        std::optional<size_t> opt_group_size = std::nullopt);
std::shared_ptr<arrow::Array> to_arrow_array(
        const std::vector<std::optional<std::string>>& array,
        size_t N);

template <typename T>
struct arrow_builder_traits {};
template <>
struct arrow_builder_traits<bool> {
    using builder_type = arrow::BooleanBuilder;
};
template <>
struct arrow_builder_traits<int32_t> {
    using builder_type = arrow::Int32Builder;
};
template <>
struct arrow_builder_traits<int64_t> {
    using builder_type = arrow::Int64Builder;
};
template <>
struct arrow_builder_traits<float> {
    using builder_type = arrow::FloatBuilder;
};
template <>
struct arrow_builder_traits<double> {
    using builder_type = arrow::DoubleBuilder;
};
template <>
struct arrow_builder_traits<std::string> {
    using builder_type = arrow::StringBuilder;
};
template <typename T>
std::shared_ptr<arrow::Array> to_arrow_array(
        const std::vector<std::optional<T>>& array)
{
    using BuilderType = typename arrow_builder_traits<T>::builder_type;
    BuilderType builder;
    for (const auto& v : array) {
        if (v.has_value()) {
            PARQUET_THROW_NOT_OK(builder.Append(*v));
        } else {
            PARQUET_THROW_NOT_OK(builder.AppendNull());
        }
    }
    std::shared_ptr<arrow::Array> result;
    PARQUET_THROW_NOT_OK(builder.Finish(&result));
    return result;
}

} // namespace testing
} // namespace parquet
} // namespace zstrong
