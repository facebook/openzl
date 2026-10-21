Examples in this directory are typically one file, and must link against the `libzstrong` library.

They can be compiled using the `Makefile` at root directory,
with `make examples`,
or more specifically `make zs2_pipeline`, `make zs2_struct` and so on.

## Databento MBP-10 parser

`mbp10.cpp` compresses MBP-10 market data (market by price, 10 book levels)
in Databento's DBN format: 368-byte `Mbp10Msg` records. The parser is a
function graph that only runs standard codecs, so its frames decode with the
universal decoder:

1. dispatch each record's fields: `ts_event`, `ts_recv`, `ts_in_delta` and
   `sequence` go to global streams, in record order; the rest of the record
   goes to a book stream;
2. dispatch the book stream by `instrument_id`;
3. dispatch each instrument's book records into one stream per field, and
   delta- then zigzag-code the price and the 60 book level fields;
4. send every stream to the Transformer graph (`ZL_GRAPH_TRANSFORMER_NUMERIC`).

Most of the gain comes from step 2: once each book field is split by
instrument, consecutive values barely change.

Build with `make mbp10` or the CMake target `mbp10`, then run:

```sh
./mbp10 --self-test
./mbp10 --generate 100000 sample.dbn   # synthetic records, not market data
./mbp10 sample.dbn sample.zl
./mbp10 -d sample.zl restored.dbn      # universal decoder only
```

The input is an uncompressed DBN file or a bare stream of DBN records. The
DBN metadata block, other record types and trailing bytes are kept verbatim,
so any input round-trips. Frames hold whole records, up to 1,048,576 of them
(`--frame-records`), and the output is a sequence of ordinary OpenZL frames.
`--no-instrument-split` and `--successor field-lz|generic` show what each
step contributes.

`--self-test` generates synthetic MBP-10 records for six instruments and
checks round trips through the universal decoder (with and without the DBN
metadata block, small frames, other record types, trailing and random bytes,
empty input), that streaming a file gives the same frames as compressing it
in memory, that the per-instrument split makes the output smaller, and that
altered frames are rejected. No exchange data is included. The record layout
follows Databento's [MBP-10 schema](https://databento.com/docs/schemas-and-data-formats/mbp-10).
