# Copyright (c) Meta Platforms, Inc. and affiliates.

import time
from typing import Any

import _openzl_demo
import numpy as np
import openzl.ext as zl
import pandas as pd
import plotly.express as px
import plotly.graph_objs as go


class TrainerResults:
    """
    A set of trained OpenZL compressors with benchmarks that can be visualized.
    """

    def __init__(self, compressors: list[bytes], inputs: list[bytes]) -> None:
        self._compressors = compressors
        self._inputs = inputs
        self._benchmarks = [benchmark(compressor, inputs) for compressor in compressors]
        self._prune()

    def _strictly_dominates(self, a: dict[str, float], b: dict[str, float]) -> bool:
        better_in_all = (
            a["compression_ratio"] >= b["compression_ratio"]
            and a["compression_speed_MBps"] >= b["compression_speed_MBps"]
            and a["decompression_speed_MBps"] >= b["decompression_speed_MBps"]
        )
        better_in_at_least_one = (
            a["compression_ratio"] > b["compression_ratio"]
            or a["compression_speed_MBps"] > b["compression_speed_MBps"]
            or a["decompression_speed_MBps"] > b["decompression_speed_MBps"]
        )
        return better_in_all and better_in_at_least_one

    def _is_non_dominated(self, index: int) -> bool:
        a = self._benchmarks[index]
        for b in self._benchmarks:
            if self._strictly_dominates(b, a):
                return False
        return True

    def _should_keep(self, index: int) -> bool:
        # Skip dominated points
        if not self._is_non_dominated(index):
            return False

        # Skip points that are very close to others
        for other in range(len(self._benchmarks)):
            if other == index:
                continue
            o = self._benchmarks[other]
            i = self._benchmarks[index]

            # Only consider pruning when the ratio is +-5% and compression speed is +-20%
            if o["compression_ratio"] < 0.95 * i["compression_ratio"]:
                continue
            if o["compression_ratio"] >= 1.05 * i["compression_ratio"]:
                continue
            if o["compression_speed_MBps"] < 0.8 * i["compression_speed_MBps"]:
                continue
            if o["compression_speed_MBps"] >= 1.2 * i["compression_speed_MBps"]:
                continue

            has_better_ratio = i["compression_ratio"] > o["compression_ratio"]
            has_much_better_dspeed = (
                i["decompression_speed_MBps"] > 1.2 * o["decompression_speed_MBps"]
            )
            has_much_worse_dspeed = (
                1.2 * i["decompression_speed_MBps"] < o["decompression_speed_MBps"]
            )

            if has_much_better_dspeed:
                # Keep if decompression speed is significantly better
                continue

            if has_better_ratio and not has_much_worse_dspeed:
                # Keep if compression ratio is better and decompression speed isn't significantly worse
                continue

            # Either the ratio is worse, or the decompression speed is significantly worse
            # Skip this point so as to not clutter the plot
            return False

        # Skip points that don't compress at all
        if self._benchmarks[index]["compression_ratio"] < 1.0:
            return False

        return True

    def _prune(self) -> None:
        """
        Prune the compressors to produce a cleaner Pareto frontier.
        1. Prune dominated points
        2. Prune points that are very close to others
        3. Prune points that don't compress at all
        """
        keep = [self._should_keep(i) for i in range(len(self._benchmarks))]
        self._compressors = [
            comp for i, comp in enumerate(self._compressors) if keep[i]
        ]
        self._benchmarks = [
            bench for i, bench in enumerate(self._benchmarks) if keep[i]
        ]

    def plot(self, **kwargs) -> go.Figure:
        """
        Plot the tradeoffs offered by the different compressors.

        Args:
            **kwargs: Additional args passed to `px.scatter()`

        Returns:
            A Plotly figure of the tradeoffs.
        """
        df = self.dataframe
        df["compressor"] = df.index
        fig = px.scatter(
            df,
            title="Trained Compressor Performance",
            x="compression_speed_MBps",
            y="compression_ratio",
            color="decompression_speed_MBps",
            text="compressor",
            hover_data=[
                "uncompressed_size",
                "compressed_size",
                "compression_time_sec",
                "decompression_time_sec",
            ],
            labels={
                "compression_speed_MBps": "Compression Speed (MB/s)",
                "compression_ratio": "Compression Ratio",
                "decompression_speed_MBps": "D. Speed (MB/s)",
                "uncompressed_size": "Uncompressed Size (bytes)",
                "compressed_size": "Compressed Size (bytes)",
                "compression_time_sec": "Compression Time (sec)",
                "decompression_time_sec": "Decompression Time (sec)",
            },
            color_continuous_scale=[
                (0, "rgb(52,50,168)"),
                (0.5, "rgb(141,46,155)"),
                (1, "rgb(238,59,37)"),
            ],
            **kwargs,
        )
        fig.update_traces(textposition="middle left", textfont_size=16, marker_size=12)
        fig.update_layout(
            title=dict(x=0.5, font_size=30),
            xaxis=dict(type="log", dtick=0.30102999566),
            font_family="Avenir",
            xaxis_title_font_size=20,
            yaxis_title_font_size=20,
            coloraxis_colorbar_title_font_size=20,
        )

        return fig

    @property
    def dataframe(self) -> pd.DataFrame:
        """
        A Pandas dataframe containing the benchmark results.
        """
        return pd.DataFrame(self._benchmarks)

    @property
    def compressors(self) -> list[bytes]:
        """
        The set of compressors. See the `dataframe` or `plot()` to select which compressor to use.
        """
        return self._compressors


def train(
    inputs: list[bytes],
    num_threads: int | None = None,
    max_time_seconds: float | None = None,
) -> TrainerResults:
    """
    Trains a set of OpenZL compressors on `inputs` that offer a Pareto frontier
    of compression ratio, compression speed, and decompression speed tradeoffs.

    Note:
        Try training on 1-10 MB of input data. Training on more than that can
        take a long time. This data should be raw numeric data like int64 or floats.

    Args:
        inputs: The list of inputs to train on.
        num_threads: Optionally the number of threads to use. Defaults to half the number of available cores.
        max_time_seconds: The maximum time to train for. Defaults to unlimited.

    Returns:
        The trained OpenZL compressors.
    """
    compressors = _openzl_demo.train(inputs, num_threads, max_time_seconds)
    return TrainerResults(compressors, inputs)


def compress(compressor: bytes, input: bytes) -> bytes:
    """
    Compresses `input` using `compressor`.

    Note:
        If `input` was not part of the training set, compression may fail.

    Args:
        compressor: A serialized compressor produced from `train()`.
        input: The data to compress.

    Returns:
        The OpenZL compressed data.
    """
    comp = zl.Compressor()
    comp.deserialize(compressor)

    cctx = zl.CCtx()
    cctx.ref_compressor(comp)
    compressed = cctx.compress(
        [zl.Input(zl.Type.Serial, np.frombuffer(input, dtype=np.uint8))]
    )
    return compressed


def decompress(compressed: bytes) -> bytes:
    """
    Decompresses data produced by `compress()`, no matter which compressor was used.

    Args:
        compressed: The OpenZL compressed data.

    Returns:
        The decompressed data.
    """
    dctx = zl.DCtx()
    decompressed = dctx.decompress(compressed)
    if len(decompressed) != 1:
        raise RuntimeError("Corruption!")
    return decompressed[0].content.as_bytes()


def benchmark(compressor: bytes, inputs: list[bytes]) -> dict[str, float]:
    """
    Benchmarks the `compressor` on the `inputs`.

    Args:
        compressor: A serialized compressor produced from `train()`.
        inputs: A list of inputs to benchmark on.

    Returns:
        The benchmark results detailing the compression ratio,
        compression speed, and decompression speed.
    """
    comp = zl.Compressor()
    comp.deserialize(compressor)

    cctx = zl.CCtx()
    dctx = zl.DCtx()

    results = {
        "uncompressed_size": float(sum(len(input) for input in inputs)),
        "compressed_size": 0.0,
        "compression_time_sec": 0.0,
        "decompression_time_sec": 0.0,
    }

    for input in inputs:
        cstart = time.perf_counter()
        cctx.ref_compressor(comp)
        compressed = cctx.compress(
            [zl.Input(zl.Type.Serial, np.frombuffer(input, dtype=np.uint8))]
        )
        cstop = time.perf_counter()

        dstart = time.perf_counter()
        decompressed = dctx.decompress(compressed)
        dstop = time.perf_counter()

        if len(decompressed) != 1:
            raise RuntimeError("Corruption!")
        if decompressed[0].content.as_bytes() != input:
            raise RuntimeError("Corruption!")

        results["compressed_size"] += len(compressed)
        results["compression_time_sec"] += cstop - cstart
        results["decompression_time_sec"] += dstop - dstart

    results["compression_ratio"] = (
        results["uncompressed_size"] / results["compressed_size"]
    )
    results["compression_speed_MBps"] = (
        results["uncompressed_size"] / 1_000_000
    ) / results["compression_time_sec"]
    results["decompression_speed_MBps"] = (
        results["uncompressed_size"] / 1_000_000
    ) / results["decompression_time_sec"]

    return results
