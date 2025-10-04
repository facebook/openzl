# OpenZL

OpenZL delivers high compression ratios _while preserving high speed_, a level of performance that is out of reach for generic compressors.

OpenZL takes a description of your data and builds from it a specialized compressor optimized for your specific format. [Learn how it works →](getting-started/introduction.md)

OpenZL consists of a core library and tools to generate specialized compressors —
all compatible with a single universal decompressor.
It is designed for engineers that deal with large quantities of specialized datasets (like AI workloads for example) and require high speed for their processing pipelines.

Here are some examples:

- [SAO], part of the [Silesia Compression Corpus](https://sun.aei.polsl.pl/~sdeor/index.php?page=silesia):

| [SAO] | [zstd](https://github.com/facebook/zstd) -3 | xz -9 | OpenZL |
| --- | --- | --- | --- |
| Ratio | x1.31 | x1.64 | **x2.06** |
| Compression Speed | 115 MB/s | 3.1 MB/s | **203 MB/s** |
| Decompression Speed | 890 MB/s | 30 MB/s | **822 MB/s** |

[SAO]: https://sun.aei.polsl.pl/~sdeor/corpus/sao.bz2

<!-- We should add more examples. For example, maybe a golden csv sample? -->

The result: **much stronger ratios than generic compressors, at the speeds required by datacenter workloads.**

Want to try it yourself? Get started in minutes with the [Quick start guide](getting-started/quick-start.md)
