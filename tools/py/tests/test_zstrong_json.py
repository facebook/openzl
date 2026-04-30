#!/usr/bin/env fbpython
# Copyright (c) Meta Platforms, Inc. and affiliates.

import json
import struct
import tempfile
import typing as t
import unittest

import numpy as np
from zstrong_json import (
    compress,
    compress_multi,
    CustomSelector,
    CustomTransform,
    DecoderCtx,
    decompress,
    decompress_multi,
    EncoderCtx,
    GCParam,
    GraphID,
    graphs,
    JsonGraph,
    read_extracted_streams,
    SelectorCtx,
    selectors,
    SimpleCustomTransform,
    SimpleDecoderCtx,
    SimpleEncoderCtx,
    StreamType,
    transforms,
)


class VariableOutputTransform(CustomTransform):
    def __init__(self, id: int) -> None:
        CustomTransform.__init__(
            self, id, [StreamType.serialized], [], [StreamType.serialized]
        )

    def encode(self, ctx: EncoderCtx) -> None:
        in_stream = ctx.get_input(0)
        assert in_stream.type() == StreamType.serialized
        data = in_stream.as_array()
        segment_size = len(data) // 10

        [
            ctx.create_output(0, data[pos : pos + segment_size])
            for pos in range(0, len(data), segment_size)
        ]

    def decode(self, ctx: DecoderCtx) -> None:
        assert len(ctx.get_fixed_inputs()) == 0
        in_streams = ctx.get_variable_inputs()
        for in_stream in in_streams:
            assert in_stream.type() == StreamType.serialized
        output = np.concatenate([in_stream.as_array() for in_stream in in_streams])
        ctx.create_output(0, output)


class MultiInputConcatTransform(CustomTransform):
    def __init__(self, id: int) -> None:
        CustomTransform.__init__(
            self,
            id,
            [StreamType.serialized, StreamType.serialized],
            [StreamType.serialized],
        )

    def encode(self, ctx: EncoderCtx) -> None:
        in_streams = ctx.get_inputs()
        assert len(in_streams) == 2
        assert in_streams[0].type() == StreamType.serialized
        assert in_streams[1].type() == StreamType.serialized

        data = in_streams[0].as_array()
        other_data = in_streams[1].as_array()
        output = np.concatenate([data, other_data])
        ctx.create_output(0, output)
        packed = struct.pack("<Q", len(data))
        ctx.send_transform_header(packed)

    def decode(self, ctx: DecoderCtx) -> None:
        ins = ctx.get_fixed_inputs()
        assert len(ins) == 1
        in_stream = ins[0]
        assert in_stream.type() == StreamType.serialized
        data = in_stream.as_array()
        header = ctx.get_transform_header()
        assert len(header) == 8
        data_size = struct.unpack("<Q", header)[0]
        ctx.create_output(0, data[:data_size])
        ctx.create_output(1, data[data_size:])


class MultiInputSwizzleTransform(CustomTransform):
    def __init__(self, id: int) -> None:
        CustomTransform.__init__(
            self,
            id,
            [StreamType.serialized, StreamType.serialized, StreamType.serialized],
            [StreamType.serialized, StreamType.serialized, StreamType.serialized],
        )

    def encode(self, ctx: EncoderCtx) -> None:
        in_streams = ctx.get_inputs()
        assert len(in_streams) == 3
        assert in_streams[0].type() == StreamType.serialized
        assert in_streams[1].type() == StreamType.serialized
        assert in_streams[2].type() == StreamType.serialized

        data0 = in_streams[0].as_array()
        data1 = in_streams[1].as_array()
        data2 = in_streams[2].as_array()

        # swizzle
        ctx.create_output(0, data1)
        ctx.create_output(1, data2)
        ctx.create_output(2, data0)

    def decode(self, ctx: DecoderCtx) -> None:
        fixed_in = ctx.get_fixed_inputs()
        assert len(fixed_in) == 3
        assert fixed_in[0].type() == StreamType.serialized
        assert fixed_in[1].type() == StreamType.serialized
        assert fixed_in[2].type() == StreamType.serialized

        data0 = fixed_in[0].as_array()
        data1 = fixed_in[1].as_array()
        data2 = fixed_in[2].as_array()

        # unswizzle
        ctx.create_output(0, data2)
        ctx.create_output(1, data0)
        ctx.create_output(2, data1)


class VariableSizeFieldTransform(CustomTransform):
    def __init__(self, id: int) -> None:
        CustomTransform.__init__(
            self,
            id,
            [StreamType.serialized],
            [StreamType.variable_size_field],
        )

    def encode(self, ctx: EncoderCtx) -> None:
        ins = ctx.get_inputs()
        assert len(ins) == 1
        input = ins[0]
        assert input.type() == StreamType.serialized
        data = input.as_bytes()
        segment_size = len(data) // 10

        output = []

        pos = 0
        while pos < len(data):
            end = min(len(data), pos + segment_size)
            output.append(data[pos:end])
            pos += segment_size

        assert isinstance(output, list)
        assert len(output) > 0
        assert isinstance(output[0], bytes)

        ctx.create_output(0, output)

    def decode(self, ctx: DecoderCtx) -> None:
        assert len(ctx.get_fixed_inputs()) == 1
        assert len(ctx.get_variable_inputs()) == 0
        input = ctx.get_fixed_inputs()[0].as_list()

        output = b"".join(input)
        ctx.create_output(0, output)


class SimpleVariableSizeFieldTransform(SimpleCustomTransform):
    def __init__(self, id: int) -> None:
        SimpleCustomTransform.__init__(
            self, id, StreamType.serialized, [StreamType.variable_size_field]
        )

    def encode(self, ctx: SimpleEncoderCtx, input: np.array) -> None:
        data = input.tobytes()
        segment_size = len(data) // 10

        output = []

        pos = 0
        while pos < len(data):
            end = min(len(data), pos + segment_size)
            output.append(data[pos:end])
            pos += segment_size

        return (output,)

    def decode(self, ctx: SimpleDecoderCtx, inputs: t.Tuple[t.List[bytes]]) -> None:
        assert len(inputs) == 1
        input = inputs[0]

        output = b"".join(input)
        return output


class SplitTransform(SimpleCustomTransform):
    def __init__(self, id: int) -> None:
        SimpleCustomTransform.__init__(
            self,
            id,
            StreamType.serialized,
            [StreamType.serialized, StreamType.serialized],
        )

    def encode(self, ctx: SimpleEncoderCtx, input: np.array) -> t.Tuple[np.array]:
        split = len(input) // 2
        return input[:split], input[split:]

    def decode(self, ctx: SimpleDecoderCtx, inputs: t.Tuple[np.array]) -> np.array:
        return np.concatenate(inputs)


class EveryOtherTransform(SimpleCustomTransform):
    def __init__(self, id: int) -> None:
        SimpleCustomTransform.__init__(
            self,
            id,
            StreamType.serialized,
            [StreamType.serialized, StreamType.serialized],
        )
        assert len(self.output_types) == 2

    def encode(self, ctx: SimpleEncoderCtx, input: np.array) -> t.Tuple[np.array]:
        if len(input) == 0:
            return input, input
        else:
            return input[0::2], input[1::2]

    def decode(self, ctx: SimpleDecoderCtx, inputs: t.Tuple[np.array]) -> np.array:
        assert len(self.output_types) == len(inputs)
        out = np.empty((inputs[0].size + inputs[1].size,), dtype=np.uint8)
        out[0::2] = inputs[0]
        out[1::2] = inputs[1]
        return out


class ReverseTransform(SimpleCustomTransform):
    def __init__(self, id: int) -> None:
        SimpleCustomTransform.__init__(
            self, id, StreamType.serialized, [StreamType.serialized]
        )

    def encode(self, ctx: SimpleEncoderCtx, input: np.array) -> t.Tuple[np.array]:
        return (input[::-1],)

    def decode(self, ctx: SimpleDecoderCtx, inputs: t.Tuple[np.array]) -> np.array:
        assert len(self.output_types) == len(inputs)
        return inputs[0][::-1]


class Int32NoopTransform(SimpleCustomTransform):
    def __init__(self, id: int) -> None:
        SimpleCustomTransform.__init__(
            self,
            id,
            StreamType.numeric,
            [StreamType.numeric],
        )

    def encode(self, ctx: SimpleEncoderCtx, input: np.array) -> t.Tuple[np.array]:
        assert input.dtype == np.uint32
        return (input,)

    def decode(self, ctx: SimpleDecoderCtx, inputs: t.Tuple[np.array]) -> np.array:
        assert inputs[0].dtype == np.uint32
        return inputs[0]


class Token8NoopTransform(SimpleCustomTransform):
    def __init__(self, id: int) -> None:
        SimpleCustomTransform.__init__(
            self,
            id,
            StreamType.fixed_size_field,
            [StreamType.fixed_size_field],
        )

    def encode(self, ctx: SimpleEncoderCtx, input: np.array) -> t.Tuple[np.array]:
        assert input.shape[1] == 8
        return (input,)

    def decode(self, ctx: SimpleDecoderCtx, inputs: t.Tuple[np.array]) -> np.array:
        assert inputs[0].shape[1] == 8
        return inputs[0]


class SelectSecond(CustomSelector):
    def __init__(self, type: StreamType) -> None:
        CustomSelector.__init__(self, type)

    def select(
        self, ctx: SelectorCtx, input: np.array, successors: t.Tuple[GraphID]
    ) -> GraphID:
        return successors[1]


class TestZstrongJsonPybind(unittest.TestCase):
    def setUp(self) -> None:
        super().setUp()

    def test_store(self) -> None:
        graph = JsonGraph('{"name": "store"}')
        data = b"some data to compress"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_global_parameters(self) -> None:
        graph = JsonGraph(graphs.zstd())
        data = b"some data to compress data data data d0ta data d0ta data compress d0ta some"
        compressed1 = compress(
            data, graph, global_params={GCParam.compression_level: 1}
        )
        compressed7 = compress(
            data, graph, global_params={GCParam.compression_level: 19}
        )
        self.assertNotEqual(compressed1, compressed7)

        decompressed1 = decompress(compressed1, graph)
        self.assertEqual(decompressed1, data)

        decompressed7 = decompress(compressed7, graph)
        self.assertEqual(decompressed7, data)

    def test_custom_graphs(self) -> None:
        fse = {"name": "fse"}
        tokenize = {
            "name": "tokenize",
            "successors": [fse, fse],
            "int_params": {0: int(StreamType.fixed_size_field), 1: False},
        }
        convert = {
            "name": "convert_serial_to_token",
            "int_params": {1: 2},
            "successors": [tokenize],
        }
        tokenize_graph = JsonGraph(json.dumps(convert))
        graphs = {"tokenize_graph": tokenize_graph}
        graph = JsonGraph('{"name": "tokenize_graph"}', custom_graphs=graphs)
        data = b"some data to compress!"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_split_custom_transform(self) -> None:
        transforms = {"split": SplitTransform(0)}
        store = {"name": "store"}
        graph = {"name": "split", "successors": [store, store]}
        graph = JsonGraph(json.dumps(graph), custom_transforms=transforms)
        data = b"some data to compress!"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_variable_output_custom_transform(self) -> None:
        transforms = {"split_variable": VariableOutputTransform(0)}
        store = {"name": "store"}
        graph = {"name": "split_variable", "successors": [store]}
        graph = JsonGraph(json.dumps(graph), custom_transforms=transforms)
        data = b"some data to compress! It is a bit longer, so the output streams are longer"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_variable_size_field_custom_transform(self) -> None:
        transforms = {"split_variable": VariableSizeFieldTransform(0)}
        store = {"name": "store"}
        graph = {"name": "split_variable", "successors": [store]}
        graph = JsonGraph(json.dumps(graph), custom_transforms=transforms)
        data = b"some data to compress! It is a bit longer, so the output streams are longer"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_simple_variable_size_field_custom_transform(self) -> None:
        custom_transforms = {"split_variable": SimpleVariableSizeFieldTransform(0)}
        prefix = transforms.prefix(graphs.store(), graphs.store())
        graph = {"name": "split_variable", "successors": [prefix]}
        graph = JsonGraph(json.dumps(graph), custom_transforms=custom_transforms)
        data = b"some data to compress! It is a bit longer, so the output streams are longer"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_int32_noop_custom_transform(self) -> None:
        custom_transforms = {"noop": Int32NoopTransform(0)}
        store = {"name": "store"}
        graph = {"name": "noop", "successors": [store]}
        graph = transforms.interpret_as_le32(graph)
        graph = JsonGraph(json.dumps(graph), custom_transforms=custom_transforms)
        data = b"0123"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_token_noop_custom_transform(self) -> None:
        custom_transforms = {"noop": Token8NoopTransform(0)}
        store = {"name": "store"}
        graph = {"name": "noop", "successors": [store]}
        graph = transforms.interpret_as_le64(graph)
        graph = JsonGraph(json.dumps(graph), custom_transforms=custom_transforms)
        data = b"01235678"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_every_other_custom_transform(self) -> None:
        transforms = {"every_other": EveryOtherTransform(0)}
        store = {"name": "store"}
        graph = {"name": "every_other", "successors": [store, store]}
        graph = JsonGraph(json.dumps(graph), custom_transforms=transforms)
        data = b"some data to compress!"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_reverse_custom_transform(self) -> None:
        transforms = {"reverse": ReverseTransform(0)}
        store = {"name": "store"}
        graph = {"name": "reverse", "successors": [store]}
        graph = JsonGraph(json.dumps(graph), custom_transforms=transforms)
        data = b"some data to compress!"

        # Test keep alive by deleting python references
        del transforms["reverse"]
        del transforms

        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_custom_selector(self) -> None:
        selectors = {"second": SelectSecond(StreamType.serialized)}
        store = {"name": "store"}
        graph = {"name": "second", "successors": [store, store]}
        graph = JsonGraph(json.dumps(graph), custom_selectors=selectors)

        # Test keep alive by deleting python references
        del selectors["second"]
        del selectors

        data = b"some data to compress!"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_standard_graph(self) -> None:
        graph = graphs.fse()
        graph = JsonGraph(graph)
        data = b"some data to compress!"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_standard_transform(self) -> None:
        graph = transforms.delta_int(graphs.fse())
        graph = transforms.interpret_as_le16(graph)
        graph = JsonGraph(graph)
        data = b"some data to compress!"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_bitunpack(self) -> None:
        graph = transforms.bitunpack(7, graphs.fse())
        graph = JsonGraph(graph)
        data = b"1234567"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_quantize_offsets(self) -> None:
        graph = transforms.quantize_offsets(graphs.fse(), graphs.store())
        graph = transforms.interpret_as_le32(graph)
        graph = JsonGraph(graph)
        data = b"012301240125"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_quantize_lengths(self) -> None:
        graph = transforms.quantize_lengths(graphs.fse(), graphs.store())
        graph = transforms.interpret_as_le32(graph)
        graph = JsonGraph(graph)
        data = b"012301240125"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_convert_serial_to_token(self) -> None:
        graph = transforms.convert_serial_to_token(2, graphs.store())
        graph = JsonGraph(graph)
        data = b"some data to compress!"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_tokenize(self) -> None:
        num_store = transforms.convert_num_to_token(graphs.store())
        fixed_store = transforms.convert_token_to_serial(graphs.store())
        vsf_store = transforms.separate_vsf_components(graphs.store(), graphs.store())

        tokenize_fixed = transforms.tokenize(
            StreamType.fixed_size_field, False, fixed_store, num_store
        )
        tokenize_numeric_unsorted = transforms.tokenize(
            StreamType.numeric, False, num_store, num_store
        )
        tokenize_numeric_sorted = transforms.tokenize(
            StreamType.numeric, True, num_store, num_store
        )

        for g in [tokenize_fixed, tokenize_numeric_unsorted, tokenize_numeric_sorted]:
            graph = transforms.interpret_as_le16(g)
            graph = JsonGraph(graph)
            data = b"0102030102030302010001020405060707070506"
            compressed = compress(data, graph)
            decompressed = decompress(compressed, graph)
            self.assertEqual(data, decompressed)

        tokenize_vsf_unsorted = transforms.tokenize(
            StreamType.variable_size_field, False, vsf_store, num_store
        )
        tokenize_vsf_sorted = transforms.tokenize(
            StreamType.variable_size_field, True, vsf_store, num_store
        )

        for g in [tokenize_vsf_unsorted, tokenize_vsf_sorted]:
            custom_transforms = {"split_variable": VariableSizeFieldTransform(0)}
            graph = {"name": "split_variable", "successors": [g]}
            graph = JsonGraph(json.dumps(graph), custom_transforms=custom_transforms)
            data = b"010202011110011212010"
            compressed = compress(data, graph)
            decompressed = decompress(compressed, graph)
            self.assertEqual(data, decompressed)

    def test_trivial_thrift(self) -> None:
        directed = selectors.directed(graphs.store())
        successors = [graphs.store()] * 12 + [directed] * 3 + [graphs.store()]
        config = b"HBsAEw8AGwAA"
        graph = transforms.thrift_compact(config, *successors)
        graph = JsonGraph(graph)
        data = b"\x00"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_json_extract(self) -> None:
        int_graph = transforms.parse_int64(
            graphs.store(), graphs.store(), graphs.store()
        )
        float_graph = transforms.parse_float64(
            graphs.store(), graphs.store(), graphs.store()
        )
        str_graph = {"name": "second", "successors": [graphs.store(), graphs.store()]}
        graph = JsonGraph(
            transforms.json_extract(graphs.store(), int_graph, float_graph, str_graph),
            custom_selectors={"second": SelectSecond(StreamType.variable_size_field)},
        )
        data = b'{"a": 1, "b": -2, "c": 3.5, "d": 0.5e-10, "e": null, "f": true, "g": false, "h": [-0, 00, 01, 00.55, 0.5E+10], "i": {}}'
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_brute_force(self) -> None:
        graph = selectors.brute_force(graphs.zstd(), graphs.huffman(), graphs.fse())
        graph = JsonGraph(graph)
        data = b"some data to compress! some data to compress. I have some more data. A little bit more data"
        compressed = compress(data, graph)
        decompressed = decompress(compressed, graph)
        self.assertEqual(data, decompressed)

    def test_extract(self) -> None:
        with tempfile.NamedTemporaryFile("wb") as alphabet_file:
            with tempfile.NamedTemporaryFile("wb") as indices_file:
                graph = transforms.tokenize(
                    StreamType.fixed_size_field,
                    False,
                    selectors.extract(alphabet_file.name, graphs.store()),
                    selectors.extract(indices_file.name, graphs.store()),
                )
                graph = transforms.convert_serial_to_token2(graph)
                graph = JsonGraph(graph)
                data = b"".join(
                    [b"00", b"ff", b"0f", b"f0", b"ff", b"f0", b"0f", b"00"]
                )

                compressed = compress(data, graph)
                decompressed = decompress(compressed, graph)
                self.assertEqual(data, decompressed)

                alphabet = read_extracted_streams(alphabet_file.name)
                indices = read_extracted_streams(indices_file.name)

                self.assertEqual(len(alphabet), 1)
                self.assertTrue(
                    np.array_equal(
                        alphabet[0],
                        np.frombuffer(b"00ff0ff0", dtype=np.uint8).reshape((4, 2)),
                    )
                )

                self.assertEqual(len(indices), 1)
                self.assertTrue(np.array_equal(indices[0], [0, 1, 2, 3, 1, 3, 2, 0]))

                compressed = compress(data, graph)
                decompressed = decompress(compressed, graph)
                self.assertEqual(data, decompressed)

    def test_multi_input(self) -> None:
        custom_transforms = {"concat": MultiInputConcatTransform(33)}
        graph = {"name": "concat", "successors": [graphs.store()]}
        graph = JsonGraph(json.dumps(graph), custom_transforms=custom_transforms)
        data = b"Hand over your money you saucy young sailor, there's plenty of bulk in your pockets I see"
        data1 = b"Aye Aye said the sailor, I've got plenty of money, but while I have life I've got none for thee"
        compressed = compress_multi([data, data1], graph)
        decompressed = decompress_multi(compressed, graph)
        self.assertEqual(len(decompressed), 2)
        self.assertEqual(data, decompressed[0])
        self.assertEqual(data1, decompressed[1])

    def test_multi_input_multi_out(self) -> None:
        custom_transforms = {"swizzle": MultiInputSwizzleTransform(22)}
        graph = {
            "name": "swizzle",
            "successors": [graphs.store(), graphs.store(), graphs.store()],
        }
        graph = JsonGraph(json.dumps(graph), custom_transforms=custom_transforms)
        data0 = b"vois sur ton chemin, gamins oublies egares, donne-leur la main pour les mener"
        data1 = b"vers d'autres lendemains, sens au coeur de la nuit, l'onde d'espoir"
        data2 = b"ardeur de la vie, sentier de gloire"
        compressed = compress_multi([data0, data1, data2], graph)
        decompressed = decompress_multi(compressed, graph)
        self.assertEqual(len(decompressed), 3)
        self.assertEqual(data0, decompressed[0])
        self.assertEqual(data1, decompressed[1])
        self.assertEqual(data2, decompressed[2])
