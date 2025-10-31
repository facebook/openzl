// Copyright (c) Meta Platforms, Inc. and affiliates.
#include <arrow/api.h>
#include <arrow/io/api.h>
#include <parquet/arrow/reader.h>
#include <parquet/arrow/writer.h>
#include <parquet/exception.h>
#include <parquet/properties.h>
#include <filesystem>
#include <iostream>
#include <string>
#include "tools/arg/arg_parser.h"
#include "tools/io/InputSetBuilder.h"

namespace {
void write_canonical_parquet_file(
        const std::string& name,
        const std::shared_ptr<arrow::Table>& table)
{
    std::shared_ptr<arrow::io::FileOutputStream> outfile;
    PARQUET_ASSIGN_OR_THROW(outfile, arrow::io::FileOutputStream::Open(name));
    auto props = parquet::WriterProperties::Builder()
                         .compression(parquet::Compression::UNCOMPRESSED)
                         ->disable_dictionary()
                         ->disable_write_page_index()
                         ->encoding(parquet::Encoding::PLAIN)
                         ->build();
    PARQUET_THROW_NOT_OK(
            parquet::arrow::WriteTable(
                    *table,
                    arrow::default_memory_pool(),
                    outfile,
                    parquet::DEFAULT_MAX_ROW_GROUP_LENGTH,
                    props));
}

std::shared_ptr<arrow::Table> get_arrow_table(
        const std::string_view& contents,
        const std::optional<size_t>& maxNumRows)
{
    auto buffer = std::make_unique<arrow::Buffer>(
            (const uint8_t*)contents.data(), contents.size());
    auto reader = std::make_shared<arrow::io::BufferReader>(std::move(buffer));
    std::unique_ptr<parquet::ParquetFileReader> parquet_reader =
            parquet::ParquetFileReader::Open(reader);
    std::unique_ptr<parquet::arrow::FileReader> arrow_reader;
    PARQUET_THROW_NOT_OK(
            parquet::arrow::FileReader::Make(
                    arrow::default_memory_pool(),
                    std::move(parquet_reader),
                    &arrow_reader));
    std::shared_ptr<arrow::Table> table;
    PARQUET_THROW_NOT_OK(arrow_reader->ReadTable(&table));
    if (maxNumRows) {
        table = table->Slice(0, *maxNumRows);
    }
    return table;
}
} // namespace

int main(int argc, char** argv)
{
    openzl::arg::ArgParser args;

    std::string kOutput     = "output";
    std::string kInput      = "input";
    std::string kRecursive  = "recursive";
    std::string kMaxNumRows = "max-num-rows";
    args.addGlobalFlag(
            kInput, 'i', true, "The input parquet file or directory");
    args.addGlobalFlag(
            kOutput,
            'o',
            true,
            "The output parquet directory. If unspecified, the output will be in the same directory as the input.");
    args.addGlobalFlag(
            kRecursive, 'r', false, "Traverse input directories recursively");
    args.addGlobalFlag(
            kMaxNumRows,
            'n',
            true,
            "The max number of rows to write to the output file. If unspecified or larger than the number of rows in an input, the output will contain all rows from the input.");

    auto parsedArgs = args.parse(argc, argv);
    if (!parsedArgs.globalHasFlag(kInput)) {
        throw std::runtime_error(
                "Please specify an input file or directory with --input");
    }

    auto inputPath  = parsedArgs.globalFlag(kInput).value();
    auto outputPath = parsedArgs.globalFlag(kOutput);
    bool recursive  = parsedArgs.globalHasFlag(kRecursive);
    std::optional<size_t> maxNumRows;
    if (parsedArgs.globalHasFlag(kMaxNumRows)) {
        maxNumRows = std::stoi(parsedArgs.globalFlag(kMaxNumRows).value());
    }

    auto inputs = openzl::tools::io::InputSetBuilder(recursive)
                          .add_path(std::move(inputPath))
                          .build();

    for (const auto& in : *inputs) {
        auto path    = std::filesystem::path(in->name());
        auto outPath = outputPath ? outputPath.value() / path.filename() : path;

        outPath.replace_extension(".parquet.canonical");

        std::cout << "Writing canonical parquet file to " << outPath << "...\n";

        auto table = get_arrow_table(in->contents(), maxNumRows);
        write_canonical_parquet_file(outPath.string(), table);
    }

    return 1;
}
