#!/usr/bin/env fbpython
# Copyright (c) Meta Platforms, Inc. and affiliates.

import json
import typing as t
import unittest

import numpy as np
from zstrong_json import (
    compress,
    decompress,
    graphs,
    JsonGraph,
    SimpleCustomTransform,
    SimpleDecoderCtx,
    SimpleEncoderCtx,
    StreamType,
    transforms,
)
from zstrong_ml import (
    FeatureGenerator,
    GBTModel,
    IntFeatureGenerator,
    MLSelector,
    MLTrainingSelector,
    samples_from_json,
    samples_to_json,
)


class MockTransform(SimpleCustomTransform):
    """
    Does nothing other than increase `self.used` in each use
    """

    def __init__(self, id: int) -> None:
        self.used = 0
        SimpleCustomTransform.__init__(
            self,
            id,
            StreamType.serialized,
            [StreamType.serialized],
        )

    def encode(self, ctx: SimpleEncoderCtx, input: np.array) -> t.Tuple[np.array]:
        self.used += 1
        return (input,)

    def decode(self, ctx: SimpleDecoderCtx, inputs: t.Tuple[np.array]) -> np.array:
        return inputs[0]


class TestZstrongMLPybind(unittest.TestCase):
    def setUp(self) -> None:
        super().setUp()

    def test_ml_selector(self) -> None:
        def test_ml_selector(a: int, b: int):
            class Features(FeatureGenerator):
                def __init__(self):
                    super().__init__(self.getFeatureNames())

                def getFeatures(self, data: np.array):
                    return {"a": int(data[0]), "b": int(data[1])}

                def getFeatureNames(self) -> t.Set[str]:
                    return {"a", "b"}

            json_model = {
                "predictor": [
                    [
                        {
                            "featureIdx": [0, -1, 1, -1, -1],
                            "value": [0.5, -1.8545455, 0.5, 1.7419355, -1.6923077],
                            "leftChildIdx": [1, -1, 3, -1, -1],
                            "rightChildIdx": [2, -1, 4, -1, -1],
                            "defaultLeft": [1, 0, 1, 0, 0],
                        },
                        {
                            "featureIdx": [1, 0, -1, -1, -1],
                            "value": [0.5, 0.5, -1.0062318, -0.843233, 0.9095943],
                            "leftChildIdx": [1, 3, -1, -1, -1],
                            "rightChildIdx": [2, 4, -1, -1, -1],
                            "defaultLeft": [1, 1, 0, 0, 0],
                        },
                    ]
                ],
                "features": ["a", "b"],
                "labels": ["zero", "one"],
            }
            model = GBTModel(json.dumps(json_model))
            mlselector = MLSelector(StreamType.serialized, model, Features())

            selectors = {"mlselector": mlselector}

            T0 = MockTransform(0)
            T1 = MockTransform(1)
            transforms = {"T0": T0, "T1": T1}
            store = {"name": "store"}
            graph = {
                "name": "mlselector",
                "successors": [
                    {"name": "T0", "successors": [store]},
                    {"name": "T1", "successors": [store]},
                ],
            }
            graph = JsonGraph(
                json.dumps(graph),
                custom_transforms=transforms,
                custom_selectors=selectors,
            )
            data = bytes([a, b, 3, 4, 5, 6])
            compressed = compress(data, graph)

            # Our model predicts a & (b ^ 1), we can use the same formula to check we are right
            self.assertNotEqual(T0.used, a & (b ^ 1))
            self.assertEqual(T1.used, a & (b ^ 1))

            decompressed = decompress(compressed, graph)
            self.assertEqual(data, decompressed)

        # Test all combinations of binary a and b
        test_ml_selector(0, 0)
        test_ml_selector(1, 0)
        test_ml_selector(0, 1)
        test_ml_selector(1, 1)

    def test_native_feature_generator_binding(self) -> None:
        arr = np.array(range(10), dtype=int)
        features = IntFeatureGenerator().getFeatures(arr)
        self.assertEqual(features["cardinality"], 10)

    def test_ml_raw_training_collector(self) -> None:
        def test_ml_raw_training_collector_on_inputs(inputs: bytes):
            selector = MLTrainingSelector(StreamType.serialized, ["store", "fse"])
            graph = {
                "name": "training_selector",
                "successors": [graphs.store(), graphs.fse()],
            }
            graph = JsonGraph(
                json.dumps(graph),
                custom_selectors={"training_selector": selector},
            )

            for inp in inputs:
                compressed = compress(inp, graph)
                decompressed = decompress(compressed, graph)
                self.assertEqual(inp, decompressed)

            collected = selector.flush_collected()
            self.assertEqual(len(collected), len(inputs))

            # We flushed, so it should be empty
            self.assertEqual(len(selector.get_collected()), 0)

            for c, inp in zip(collected, inputs):
                self.assertEqual(c.data.dtype, np.uint8)
                self.assertEqual(c.data.tobytes(), inp)
                self.assertEqual(set(c.targets.keys()), {"store", "fse"})

        test_ml_raw_training_collector_on_inputs([b"1234567890"])
        test_ml_raw_training_collector_on_inputs([b""])
        test_ml_raw_training_collector_on_inputs(
            [b"1234567890", b"1" * 1000, b"abcdef1234\x00\x01\x02\x03\n"]
        )

    def test_ml_raw_training_collector_numeric(self) -> None:
        selector = MLTrainingSelector(StreamType.numeric, ["store"])
        graph = {
            "name": "training_selector",
            "successors": [graphs.store()],
        }
        graph = transforms.interpret_as_le32(graph)
        graph = JsonGraph(
            json.dumps(graph),
            custom_selectors={"training_selector": selector},
        )

        data = np.array([1, 2, 3, 4], dtype=np.uint32)
        compress(data.tobytes(), graph)
        collected = selector.get_collected()
        self.assertEqual(len(collected), 1)
        self.assertEqual(collected[0].data.dtype, data.dtype)
        self.assertTrue(np.array_equal(collected[0].data, data))
        self.assertEqual(list(collected[0].targets.keys()), ["store"])

    def test_ml_raw_training_collector_advanced_params(self) -> None:
        def test_params(collect_inputs, feature_generator):
            selector = MLTrainingSelector(
                StreamType.numeric,
                ["store"],
                collect_inputs=collect_inputs,
                feature_generator=feature_generator,
            )
            graph = {
                "name": "training_selector",
                "successors": [graphs.store()],
            }
            graph = transforms.interpret_as_le32(graph)
            graph = JsonGraph(
                json.dumps(graph),
                custom_selectors={"training_selector": selector},
            )
            data = np.array([1, 2, 3, 4], dtype=np.uint32)
            compress(data.tobytes(), graph)
            collected = selector.get_collected()
            # Serialize and deserialize to check that this flow works
            collected = samples_from_json(samples_to_json(collected))
            if collect_inputs:
                self.assertIsNotNone(collected[0].data)
            else:
                self.assertIsNone(collected[0].data)

            if feature_generator is not None:
                self.assertEqual(collected[0].features["len"], len(data))
            else:
                self.assertEqual(len(collected[0].features), 0)

        class MockFeatureGenerator(FeatureGenerator):
            def __init__(self):
                super().__init__(self.getFeatureNames())

            def getFeatures(self, data: np.array):
                return {"len": len(data)}

            def getFeatureNames(self) -> t.Set[str]:
                return {"len"}

        for collect_input in [True, False]:
            for feature_generator in [None, MockFeatureGenerator()]:
                test_params(collect_input, feature_generator)
