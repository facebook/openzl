# Quick Start

This quick start gives you a hands-on walk-through of the `zli` CLI so you can begin compressing data with OpenZL right away. Before you start, make sure your environment has `git`, a C++ compiler (such as `g++` or `clang++`), and `make` installed.

This 15 minute exercise will teach you:

- How to compress some simple data (a vector of numerics) using a preset profile.
- How to decompress (and verify) any OpenZL compressed frame.
- How to employ the trainer to find a better solution.
- How to compress using a configuration provided by the trainer.
- How to trace and visualize a configured compression graph.

## Clone OpenZL

```sh
git clone --depth 1 -b release https://github.com/facebook/openzl.git
cd openzl
```

## Building the OpenZL CLI

OpenZL consists of a core library and a set of tools, each of which can be compiled independently. However, for usage simplicity, many of them are bundled into a CLI called `zli`.
Compile it; it will be your main tool for the next sections.

```sh
make zli
```

??? note "Alternative: Using cmake instead of make"
    ```bash
    mkdir -p cmakebuild
    cmake -S . -B cmakebuild
    cmake --build cmakebuild --target zli
    ln -sf cmakebuild/cli/zli .
    ```

This command compiles `zli` and then links it into the current directory, so it can be invoked with `./zli`.
The CLI features many commands, which can be discovered with `./zli --help`.

## First compression scenario

For this first example usage, you are going to compress some simple data of known type.
To do that, you need to provide a “profile” to the compressor.

A `zli` “profile” is a pre-built description of the data's structure that helps the compressor understand how to best process it. For example, a profile might specify that the data is an array of 64-bit integers, a csv file, or another format. By specifying a profile, you guide OpenZL to use compression techniques that are optimized for that specific kind of data, resulting in better compression ratios and performance compared to generic methods.

`zli` supports a number of pre-built data profiles out of the box, which can be listed with:

```sh
./zli list-profiles
```

For this first example, you will compress `sra0`, which is the first column of the [`sao`](https://sun.aei.polsl.pl/~sdeor/corpus/sao.bz2) sample from the Silesia compression corpus, and is just an array of 64-bit numeric values.

First, download the sample. You can use your browser to point at [https://github.com/facebook/openzl/releases/tag/openzl-sample-artifacts](https://github.com/facebook/openzl/releases/tag/openzl-sample-artifacts).
Alternatively, you can use the following script:

```sh
wget https://github.com/facebook/openzl/releases/download/openzl-sample-artifacts/sra0.zip
unzip sra0.zip
rm sra0.zip
```

??? note "Alternative: use `curl` instead of `wget`"
    ```bash
    curl -L -O https://github.com/facebook/openzl/releases/download/openzl-sample-artifacts/sra0.zip
    unzip sra0.zip
    rm sra0.zip
    ```

This will install the file `sra0` in the current directory.

For comparison's sake, compress it with known generic compression algorithms:

| Algorithm  | Compressed Size | Ratio |
| ---------- | --------------- | ----- |
| `gzip -9`  |       1,742,445 | 1.19x |
| `zstd -19` |       1,696,778 | 1.22x |
| `xz -9`    |       1,488,144 | 1.39x |

Now compress it with OpenZL's `zli`. Note the requirement to provide a `--profile` to instruct OpenZL how to interpret the data:

```sh
./zli compress --profile le-u64 sra0 --output sra0.zl
```

This will result in a file `sra0.zl`.
At the time of this writing, performance is expected to be:

```
Compressed 2071976 -> 1378167 (1.50x) in 15.335 ms, 128.85 MiB/s
```

Note that the compression ratio is better than all generic algorithms tested before, and this is achieved while maintaining a high compression speed.

### Decompression and verification

Ensure that the compressed data can be decoded by using the following command:

```sh
./zli decompress sra0.zl --output sra0.decompressed
```

which should give you:

```
Decompressed: 66.51% (1345.87 KiB -> 2023.41 KiB) in 2.619 ms, 754.60 MiB/s
```

Note that decompression doesn't require any `--profile`: data compressed with `openzl` is always decodable, regardless of which `--profile` was used for compression. The profile only impacts compression effectiveness, not compatibility.

Now compare the resulting decompressed file with the original to be sure they are exactly identical:

```sh
cmp sra0 sra0.decompressed
```

which results in nothing (meaning no difference).

???+ tip "Alternative: use `md5sum` instead of `cmp`"
     ```bash
     md5sum sra0 sra0.decompressed
     ```
     which results in:
     ```
     22d1812abe498f23fffb479857cfad19  sra0
     22d1812abe498f23fffb479857cfad19  sra0.decompressed
     ```

## Training

The previous example was using a predefined profile, which is still somewhat generic, and therefore not specialized for the sample provided.
The goal of OpenZL is to generate specialized compressors, optimized for user's input.
To this end, you are going to train a new profile and use it to compress the data.
The approach makes more sense when compressing a large flow of similar data, because the cost of training is then amortized across all future compressions.
In this example, you will use a single sample for simplicity.

Now train a compressor using the `sra0` sample:

```sh
./zli train --profile le-u64 sra0 --output sra0.zlc
```

??? note "Setting max training time"
    The trainer is multi-threaded, and machines with less cores will take longer
    to train. If it is taking too long, the `--max-time-secs` flag can be used to
    limit the training time. It currently limits the time spent in each step of
    training, not the total time. But, the trained result may be worse given less
    time to train.

The training session will generate some messages on the consoles.
The last lines should look something like that:

```sh
[==================================================] Training ACE graph 1/1
Benchmarking trained compressor...
1 files: 2071976 -> 597635 (3.47),  121.64 MB/s  1059.97 MB/s
Training improved compression ratio by 130.48%
```

??? note "Variability in training results"
    Training is non-deterministic, meaning results from training vary a bit between sessions.
    However, results are expected to stay within a reasonable margin, meaning compression ratio and speed should remain roughly comparable between sessions.

The generated `sra0.zlc` is a serialized compressor, specialized for the trained data.
It is used to tell `zli` (or the library) how to compress the data.

### Compress with trained profile

<!-- Note: since the result of ace is not reproducible, it can differ between runs.
     Should we have a downloadable "golden config" to better control the rest of the story ? -->

Now compress `sra0` with this newly trained profile:

```sh
./zli compress sra0 --compressor sra0.zlc --output sra0.zl
```

Expect a result similar to the one observed at end of training:

```sh
Compressed 2071976 -> 597635 (3.47x) in 18.239 ms, 108.34 MiB/s
```

Following a procedure similar to the previous exercise, you can now decompress `sra0.zl`
and check that it is effectively identical to the original `sra0`.

This exercise shows the importance of training to generate enhanced solutions for homogeneous datasets.

### Find the Pareto frontier of compressors

The previous training command searched only for the smallest compressed size.
Adding the `--pareto-frontier` flag makes the trainer output the entire Pareto frontier of optimal tradeoffs of compression ratio, compression speed, and decompression speed.
Instead of outputting a single compressor, it will create a directory of compressors, along with the benchmark results in `benchmark.csv`.

```
./zli train --profile le-u64 sra0 --output compressors/ --pareto-frontier
```

The `compressors/` directory will look like this:

```
compressors/
├─ benchmark.csv
├─ 0.zc
├─ 1.zc
├─ 2.zc
├─ ...
```

Opening up `benchmark.csv` will show the performance of each compressor as show below.

```
Algorithm, Compressor, Compression Ratio, Compression Speed MB/s, Decompression Speed MB/s
   OpenZL,       0.zc,              3.44,                  82.30,                   859.41
   OpenZL,       1.zc,              3.44,                  74.72,                   890.28
   OpenZL,       2.zc,              3.41,                  63.51,                   995.84
   OpenZL,       3.zc,              3.37,                  71.48,                  1068.47
   OpenZL,       4.zc,              3.36,                  79.40,                  1086.73
   OpenZL,       5.zc,              3.36,                  74.87,                  1082.06
   OpenZL,       6.zc,              2.87,                  91.93,                  1360.87
   OpenZL,       7.zc,              2.87,                  89.69,                  1319.01
   OpenZL,       8.zc,              2.86,                  87.85,                  1301.64
   OpenZL,       9.zc,              2.17,                 112.89,                   781.85
   OpenZL,      10.zc,              2.17,                 114.48,                   772.73
   OpenZL,      11.zc,              2.16,                 126.49,                   862.90
   OpenZL,      12.zc,              2.16,                 148.20,                   891.53
   OpenZL,      13.zc,              2.16,                 177.49,                   877.77
   OpenZL,      14.zc,              1.53,                 161.59,                  1284.70
   OpenZL,      15.zc,              1.53,                 155.75,                  1193.33
   OpenZL,      16.zc,              1.53,                 205.63,                  1413.22
   OpenZL,      17.zc,              1.53,                 199.96,                  1480.62
   OpenZL,      18.zc,              1.53,                 209.57,                  1461.65
   OpenZL,      19.zc,              1.52,                 268.06,                  1301.43
   OpenZL,      20.zc,              1.52,                 509.61,                   724.33
   OpenZL,      21.zc,              1.52,                 798.76,                  1175.44
   OpenZL,      22.zc,              1.50,                 111.81,                  2087.11
   OpenZL,      23.zc,              1.45,                 300.39,                  2084.80
   OpenZL,      24.zc,              1.45,                 229.43,                  2124.02
   OpenZL,      25.zc,              1.42,                 791.30,                  2215.84
   OpenZL,      26.zc,              1.42,                 832.52,                  1507.42
   OpenZL,      27.zc,              1.42,                 806.65,                  1513.20
   OpenZL,      28.zc,              1.02,                1235.24,                  1670.63
   OpenZL,      29.zc,              1.02,                2027.32,                  3553.12
   OpenZL,      30.zc,              1.00,                2663.61,                  3846.78
   OpenZL,      31.zc,              1.00,                1489.72,                  5519.57
```

At this point, you can pick the compressor from `benchmark.csv` with the tradeoff that fits your needs.

??? note "Pruning"
    Currently, the trainer produces too many compressors that have tradeoffs that are very close.
    We are currently working on pruning the number of choices down.

## Visualizing the Compression Graph

The compression graph is the ultimate driver of both compression and decompression.
This example will give an intuitive understanding of the graph through a hands-on visualization.
The graph is described in greater detail in [Introduction](introduction.md) and [Concepts](concepts.md).

Visualization uses the same record/report flow that `perf` and other tracing tools uses. First, to capture a trace, add a `--trace` argument to a compression:

```sh
./zli compress sra0 --compressor sra0.zlc --output /dev/null --trace sra0.cbor
```

This will write some lines to the console. Check for a successful trace

```
Successfully wrote streamdump CBOR
```

Then visit the [Trace Visualizer](https://facebook.github.io/openzl/tools/trace) and input the trace file (in the example, `sra0.cbor`).
The visualization is fully interactive. More usage details are available via the help menu in the visualizer.

??? note "Your data is private!"
    The visualizer is a static webpage.
    It does not access the network nor write any cookies.
    Your data will NOT be shared anywhere.
    The code for this webpage is available [here](https://github.com/facebook/openzl/tree/release/tools/visualization_app).

## Next steps

- Read the [Introduction](introduction.md) and [Concepts](concepts.md) pages to learn more about OpenZL.
- Check out [more compiled tools](more-tools.md).
- Check out the [numeric_array](examples/c/numeric-array.md) example.
- Check out the [how to use OpenZL](using-openzl.md) for getting started with custom compression.
- Check out our [API Reference](../api/c/compressor.md).
- See our [PyTorch Model Compressor](https://github.com/facebook/openzl/blob/release/custom_parsers/pytorch_model_compressor.cpp) for an example of a complete production compressor.
