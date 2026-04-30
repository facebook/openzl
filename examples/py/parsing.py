# Copyright (c) Meta Platforms, Inc. and affiliates.

# --8<-- [start:imports]
import random
import struct
from typing import List

import openzl.ext as zl

# --8<-- [end:imports]

"""
Setup from PyPI:

    pip install openzl

Setup from OpenZL repo:

    cd py
    pip install .
"""

# --8<-- [start:generate_test_data]

NUM_SAMPLES = 10


def generate_test_data() -> List[bytes]:
    FMT = {
        1: "B",
        2: "H",
        4: "I",
        8: "Q",
    }

    rng = random.Random(0)
    samples = []
    for _ in range(NUM_SAMPLES):
        sample = []
        for _ in range(100):
            elt_width = rng.choice([1, 2, 4, 8])
            num_elts = rng.randint(1000, 10000)
            max_value = rng.randint(127, 255)

            values = [rng.randint(0, max_value) for _ in range(num_elts)]
            sample.append(
                struct.pack(
                    f"<IB{num_elts}{FMT[elt_width]}", num_elts, elt_width, *values
                )
            )
        samples.append(b"".join(sample))

    return samples


TEST_DATA_SET = generate_test_data()
TEST_DATA_SET_SIZE = sum(len(s) for s in TEST_DATA_SET)

# --8<-- [end:generate_test_data]


# --8<-- [start:parsing_function_graph]


class ParsingFunctionGraph(zl.FunctionGraph):
    """
    Parses data in the format:
    [4-byte number of elements]
    [1-byte element width]
    [(num_elts * element_width) bytes of data]
    """

    def __init__(self) -> None:
        super().__init__()

    def function_graph_description(self) -> zl.FunctionGraphDescription:
        return zl.FunctionGraphDescription(
            name="parsing_function_graph",
            input_type_masks=[zl.TypeMask.Serial],
        )

    def graph(self, state: zl.GraphState) -> None:
        # Get the input data to the graph
        data = state.edges[0].input.content.as_bytes()

        # We'll split the data into different outputs.
        # Each metadata field gets its own output
        NUM_ELTS_TAG = 0
        ELT_WIDTH_TAG = 1
        # Each integer width gets its own output
        UINT8_TAG = 2
        UINT16_TAG = 3
        UINT32_TAG = 4
        UINT64_TAG = 5
        NUM_TAGS = 6

        WIDTH_TO_TAG = {
            1: UINT8_TAG,
            2: UINT16_TAG,
            4: UINT32_TAG,
            8: UINT64_TAG,
        }

        # Lex the input into tags identifying each segment, and the size of that segment.
        tags = []
        sizes = []

        while len(data) > 0:
            if len(data) < 5:
                raise ValueError("Invalid data: bad header")
            num_elts, elt_width = struct.unpack("<IB", data[:5])
            data = data[5:]
            if elt_width not in {1, 2, 4, 8}:
                raise ValueError(f"Invalid element width: {elt_width}")
            if num_elts * elt_width > len(data):
                raise ValueError(
                    f"Invalid number of elements: {num_elts} * {elt_width} > {len(data)}"
                )

            # Lex the metadata
            tags.append(NUM_ELTS_TAG)
            sizes.append(4)
            tags.append(ELT_WIDTH_TAG)
            sizes.append(1)

            # Lex the data
            tags.append(WIDTH_TO_TAG[elt_width])
            sizes.append(num_elts * elt_width)

            data = data[num_elts * elt_width :]

        # Run the dispatch node. This creates two singleton streams: the tags and sizes.
        # It also creates NUM_TAGS streams, one for each tag following those.
        dispatch = zl.nodes.DispatchSerial(
            segment_tags=tags, segment_sizes=sizes, num_tags=NUM_TAGS
        )
        (
            tags_edge,
            sizes_edge,
            num_elts_edge,
            elt_width_edge,
            uint8_edge,
            uint16_edge,
            uint32_edge,
            uint64_edge,
        ) = dispatch.run(state.edges[0])

        # We've handled the input edge. Now every newly created edge needs to be handled.

        # The tags and sizes edges are already numeric, so we just send them to the numeric graph.
        self._numeric_graph(tags_edge)
        self._numeric_graph(sizes_edge)

        # The dispatched edges are all serial. They need to be converted to numeric first.
        self._conversion_graph(num_elts_edge, width=4)
        self._conversion_graph(elt_width_edge, width=1)
        self._conversion_graph(uint8_edge, width=1)
        self._conversion_graph(uint16_edge, width=2)
        self._conversion_graph(uint32_edge, width=4)
        self._conversion_graph(uint64_edge, width=8)

    def _numeric_graph(self, edge: zl.Edge) -> None:
        # Just send the data to the generic numeric compression graph
        zl.graphs.Compress().set_destination(edge)

    def _conversion_graph(self, edge: zl.Edge, width: int) -> None:
        # Convert the data to numeric in little-endian format with the given width.
        conversion = zl.nodes.ConvertSerialToNumLE(int_size_bytes=width)
        edge = conversion.run(edge)[0]
        # Send the converted edge to the numeric graph
        self._numeric_graph(edge)


# --8<-- [end:parsing_function_graph]

# --8<-- [start:compressor]


def build_parsing_compressor() -> zl.Compressor:
    compressor = zl.Compressor()
    graph = compressor.register_function_graph(ParsingFunctionGraph())
    compressor.select_starting_graph(graph)
    return compressor


# --8<-- [end:compressor]


# --8<-- [start:measurement]
def build_zstd_compressor() -> zl.Compressor:
    compressor = zl.Compressor()
    graph = zl.graphs.Zstd()(compressor)
    compressor.select_starting_graph(graph)
    return compressor


def compress(compressor: zl.Compressor, data: bytes) -> bytes:
    cctx = zl.CCtx()

    # Tell the cctx to use the compressor we've built.
    cctx.ref_compressor(compressor)

    # Select the OpenZL format version to encode with.
    # This should be the latest version that the decompressor supports.
    cctx.set_parameter(zl.CParam.FormatVersion, zl.MAX_FORMAT_VERSION)

    # Compress a single input of serial data.
    return cctx.compress([zl.Input(zl.Type.Serial, data)])


def decompress(compressed: bytes) -> bytes:
    dctx = zl.DCtx()

    # Decompress the compressed data.
    # OpenZL compressed data can decompress to multiple outputs of several types.
    # Here, we expect to receive a single output of serial data.
    outputs = dctx.decompress(compressed)
    if len(outputs) != 1 or outputs[0].type != zl.Type.Serial:
        raise RuntimeError("Only one serial output supported")

    # Convert the OpenZL output to bytes.
    # This costs a copy, but is simple. The output data can also be accessed as a NumPy array with zero copies.
    return outputs[0].content.as_bytes()


def test_compressor(compressor: zl.Compressor, dataset: List[bytes]) -> int:
    compressed_size = 0
    for data in dataset:
        compressed = compress(compressor, data)
        decompressed = decompress(compressed)
        if decompressed != data:
            raise RuntimeError("Corruption!")
        compressed_size += len(compressed)
    return compressed_size


compressor = build_zstd_compressor()
compressed_size = test_compressor(compressor, TEST_DATA_SET)
print(f"Zstd compressor ratio: {TEST_DATA_SET_SIZE / compressed_size:.2f}")

compressor = build_parsing_compressor()
compressed_size = test_compressor(compressor, TEST_DATA_SET)
print(f"Parsing compressor ratio: {TEST_DATA_SET_SIZE / compressed_size:.2f}")


# --8<-- [end:measurement]
