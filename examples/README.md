Examples in this directory are typically one file, and must link against the `libzstrong` library.

They can be compiled using the `Makefile` at root directory,
with `make examples`,
or more specifically `make zs2_pipeline`, `make zs2_struct` and so on.


## Nasdaq BinaryFILE ITCH parser

`itch.cpp` demonstrates two levels of structural parsing: dispatch complete
length-prefixed records by message type, then split each fixed-layout record
into fields. Integer columns use the stock big-endian conversion and numeric
compression graphs. The six-byte timestamp is represented as high16 and low32
columns. This is a compressor graph, with no custom transform or decoder.
The standard OpenZL decoder restores the complete original byte stream,
including length prefixes, record ordering, unknown message types, and every
field. No order-book reconstruction is needed.

Build with `make itch`, or build the CMake target `itch`. Then run:

```sh
./itch --self-test
./itch sample.itch sample.zl
```

The example reads one input of up to 64 MiB containing complete BinaryFILE
records and writes one ordinary OpenZL frame. It refuses existing outputs.
The input is the decompressed BinaryFILE stream, not its gzip delivery wrapper
or a MoldUDP network capture. Applications handling whole days should split at
record boundaries; framing and indexing those separate outputs is their own
responsibility. Compression levels and downstream graphs can be varied without
changing the decoder. The example uses level 9 and a stock four-way numeric
selector; it is not trained and does not claim an optimal ITCH profile.

`--self-test` generates all supported record shapes, integer extremes, unknown
and noncanonical records, and a larger varying sequence; it checks ordinary
OpenZL decoding and rejects altered compressed content. No exchange data is
included. Field layouts follow the official
[Nasdaq TotalView-ITCH5.0 specification](https://nasdaqtrader.com/content/technicalsupport/specifications/dataproducts/NQTVITCHSpecification.pdf).
