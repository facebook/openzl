// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "custom_parsers/parquet/tests/test_utils.h"

#include <arrow/io/api.h> // @manual
#include <parquet/arrow/writer.h>
#include <parquet/exception.h>

namespace zstrong {
namespace parquet {
namespace testing {
std::string to_canonical_parquet(
        const std::shared_ptr<arrow::Table> table,
        std::optional<size_t> opt_group_size)
{
    size_t group_size =
            opt_group_size.value_or(::parquet::DEFAULT_MAX_ROW_GROUP_LENGTH);
    PARQUET_ASSIGN_OR_THROW(auto out, arrow::io::BufferOutputStream::Create());
    auto props = ::parquet::WriterProperties::Builder()
                         .compression(::parquet::Compression::UNCOMPRESSED)
                         ->disable_dictionary()
                         ->disable_write_page_index()
                         ->encoding(::parquet::Encoding::PLAIN)
                         ->build();
    PARQUET_THROW_NOT_OK(
            ::parquet::arrow::WriteTable(
                    *table,
                    arrow::default_memory_pool(),
                    out,
                    group_size,
                    props));
    PARQUET_ASSIGN_OR_THROW(auto buffer, out->Finish());
    return buffer->ToString();
}

std::shared_ptr<arrow::Array> to_arrow_array(
        const std::vector<std::optional<std::string>>& array,
        size_t N)
{
    arrow::FixedSizeBinaryBuilder builder(arrow::fixed_size_binary(N));
    for (const auto& v : array) {
        if (v.has_value()) {
            assert(v->size() == N);
            PARQUET_THROW_NOT_OK(builder.Append(v->data()));
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
