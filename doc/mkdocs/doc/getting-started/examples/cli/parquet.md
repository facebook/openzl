# Parquet Compression
The OpenZL `parquet` profile supports compression of Parquet files that are presented in a "canonical" format. This example shows how to canonicalize and compress a Parquet file using the OpenZL `make_canonical_parquet` and `cli` tools.

## Canonicalization
In order to be compressed using the `parquet` profile, Parquet files must first be converted to a canonical format. The canonical format requires all columns to be uncompressed and plain encoded.

You can use the `make_canonical_parquet` tool from the `tools/parquet` directory to convert your files to the expected format.

The canonicalization tool takes a dependency on parquet and arrow, and is thus not built by default. You can build the tool by passing the following flag when building the OpenZL library with CMake:
```
cmake -DOPENZL_BUILD_PARQUET_TOOLS=ON
```

Once built, you can use the tool to convert some input Parquet file or directory into the canonical format.
```
make_canonical_parquet --input <input_file_or_directory> --output <output_dir>
```

If the given parquet file is too large to compress with OpenZL, the script can also be used to truncate the input file for experimentation.
```
make_canonical_parquet --input <input_file_or_directory> --output <output_dir> --max-num-rows <num_rows>
```

## Compression
Once your Parquet files have been converted to the canonical format, you can use the `parquet` profile in the zli to compress them.

```
zli compress <input_file> --profile parquet
```

You can also run training to potentially improve the performance of the Parquet compressor on your data.
```
zli train <sample_dir> --profile parquet --output <trained>
```
```
zli compress <input_file> --compressor <trained>
```
