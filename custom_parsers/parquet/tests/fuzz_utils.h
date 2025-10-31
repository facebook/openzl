// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <arrow/api.h>
#include <parquet/exception.h>
#include <string>
#include <vector>

#include "custom_parsers/parquet/tests/test_utils.h"
#include "tests/fuzz_utils.h"

namespace zstrong {
namespace parquet {
namespace testing {
using namespace facebook::security::lionhead::fdp;

template <typename Mode>
std::shared_ptr<arrow::Field> gen_arrow_field(
        StructuredFDP<Mode>& f,
        size_t maxDepth,
        size_t depth,
        size_t idx)
{
    bool isLeaf = maxDepth == depth ? true : f.coin("is_leaf");
    // Hack to enforce unique names for the same struct
    auto name = tests::gen_str(f, "field_name", Range<size_t>(1, 10));
    name += std::to_string(name.size()) + std::to_string(idx);

    std::shared_ptr<arrow::DataType> type;

    if (isLeaf) {
        auto fixed = arrow::fixed_size_binary(1);
        type       = f.choices(
                "data_type",
                { arrow::boolean(),
                        arrow::int32(),
                        arrow::int64(),
                        arrow::float32(),
                        arrow::float64(),
                        arrow::utf8(),
                        fixed });
        if (type == fixed) {
            type = arrow::fixed_size_binary(
                    f.u16_range("fixed_len_byte_array_width", 1, 32));
        }
    } else {
        size_t numChildren = f.u16_range("num_children", 1, 10);
        std::vector<std::shared_ptr<arrow::Field>> fields(numChildren);
        for (size_t i = 0; i < numChildren; ++i) {
            fields[i] = gen_arrow_field(f, maxDepth, depth + 1, i);
        }
        type = arrow::struct_(fields);
    }

    return arrow::field(std::move(name), std::move(type));
};

template <typename Mode>
std::shared_ptr<arrow::Schema> gen_arrow_schema(
        StructuredFDP<Mode>& f,
        size_t maxDepth = 0)
{
    // Recursively generate children
    size_t numChildren = f.u8_range("num_children", 1, 20);
    std::vector<std::shared_ptr<arrow::Field>> fields(numChildren);
    for (size_t i = 0; i < numChildren; ++i) {
        fields[i] = gen_arrow_field(f, maxDepth, 0, i);
    }
    return arrow::schema(std::move(fields));
};

template <typename Mode, typename T>
std::vector<std::optional<T>>
gen_vec(StructuredFDP<Mode>& f, std::string& name, size_t numElts)
{
    std::vector<std::optional<T>> vec(numElts);
    for (size_t i = 0; i < numElts; ++i) {
        if (!f.has_more_data() || f.coin("null")) {
            vec[i] = std::nullopt;
        } else {
            vec[i] = Uniform<T>().gen(name, f);
        }
    }
    return vec;
}

template <typename Mode, typename LenDist>
std::vector<std::optional<std::string>> gen_str_vec(
        StructuredFDP<Mode>& f,
        std::string& name,
        size_t numElts,
        LenDist lenDist)
{
    std::vector<std::optional<std::string>> vec(numElts);
    for (size_t i = 0; i < numElts; ++i) {
        if (!f.has_more_data() || f.coin("null")) {
            vec[i] = std::nullopt;
        } else {
            vec[i] = tests::gen_str(f, name, lenDist);
        }
    }
    return vec;
}

template <typename Mode>
std::shared_ptr<arrow::Array> gen_array_from_field(
        StructuredFDP<Mode>& f,
        const std::shared_ptr<arrow::Field>& field,
        size_t numElts)
{
    auto type     = field->type();
    auto typeName = type->name();

    if (typeName == "bool") {
        return to_arrow_array(gen_vec<Mode, bool>(f, typeName, numElts));
    } else if (typeName == "int32") {
        return to_arrow_array(gen_vec<Mode, int32_t>(f, typeName, numElts));
    } else if (typeName == "int64") {
        return to_arrow_array(gen_vec<Mode, int64_t>(f, typeName, numElts));
    } else if (typeName == "float") {
        return to_arrow_array(gen_vec<Mode, float>(f, typeName, numElts));
    } else if (typeName == "double") {
        return to_arrow_array(gen_vec<Mode, double>(f, typeName, numElts));
    } else if (typeName == "utf8") {
        return to_arrow_array(
                gen_str_vec(f, typeName, numElts, Range<size_t>(1, 100)));
    } else if (typeName == "fixed_size_binary") {
        auto width = type->byte_width();
        return to_arrow_array(
                gen_str_vec(f, typeName, numElts, Const<size_t>(width)), width);
    } else if (typeName == "struct") {
        std::vector<std::shared_ptr<arrow::Array>> arrays;
        for (const auto& field : field->type()->fields()) {
            arrays.push_back(gen_array_from_field(f, field, numElts));
        }
        return std::make_shared<arrow::StructArray>(
                arrow::StructArray(type, numElts, arrays, nullptr, 0, 0));
    } else {
        throw std::runtime_error("Unsupported type: " + typeName);
    }
};

template <typename Mode>
std::string gen_parquet_from_schema(
        StructuredFDP<Mode>& f,
        const std::shared_ptr<arrow::Schema>& schema)
{
    size_t numFields = schema->fields().size();
    std::vector<std::shared_ptr<arrow::Array>> arrays(numFields);

    size_t numElts = f.u32_range("num_elts", 1, 5000);

    for (size_t i = 0; i < numFields; i++) {
        arrays[i] = gen_array_from_field(f, schema->fields()[i], numElts);
    }
    return to_canonical_parquet(arrow::Table::Make(schema, arrays));
};

} // namespace testing
} // namespace parquet
} // namespace zstrong
