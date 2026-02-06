#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.

import argparse
import os
from collections import defaultdict

import pandas as pd
import plotly.graph_objects as go


class ParetoFrontier:
    def __init__(self) -> None:
        self._frontier = defaultdict(list)

    def _strictly_dominates(self, x, y):
        return all(x[i] >= y[i] for i in range(len(x))) and any(
            x[i] > y[i] for i in range(len(x))
        )

    def add(self, algo, key, *x) -> None:
        if any(self._strictly_dominates(y[1:], x) for y in self._frontier[algo]):
            return
        self._frontier[algo] = [
            y for y in self._frontier[algo] if not self._strictly_dominates(x, y[1:])
        ]
        self._frontier[algo].append([key] + list(x))
        self._frontier[algo].sort(key=lambda x: x[1:])

    def frontier(self, algo):
        f = []
        for i in range(len(self._frontier[algo][0])):
            f.append([x[i] for x in self._frontier[algo]])
        return f

    def contains(self, algo, key) -> None:
        return key in {x[0] for x in self._frontier[algo]}


def pareto_frontier(df, x_axis):
    frontier = ParetoFrontier()
    for idx, (algo, speed, ratio) in enumerate(
        zip(df["algorithm"], df[x_axis], df["compression_ratio"])
    ):
        if ratio < 1:
            continue
        frontier.add(algo, idx, speed, ratio)
    return frontier


def scatter(name, df, x_axis):
    _idx, x, y = pareto_frontier(df, x_axis).frontier(name)
    return go.Scatter(x=x, y=y, mode="lines+markers", name=name)


def plot(title, algorithm_dfs, x_axis):
    x_axis_name = {
        "compress_speed_mbps": "Compression Speed",
        "decompress_speed_mbps": "Decompression Speed",
    }
    labels = {
        "compression_ratio": "Compression Ratio",
        x_axis: f"{x_axis_name[x_axis]} (MB/s)",
    }

    fig = go.Figure()
    for algo, df in algorithm_dfs.items():
        fig.add_trace(scatter(algo, df, x_axis))

    fig.update_layout(
        title=f"{title}: {x_axis_name[x_axis]} vs. Compression Ratio",
        title_x=0.5,
        xaxis_title=labels[x_axis],
        yaxis_title=labels["compression_ratio"],
        xaxis_type="log",
        xaxis_tickvals=[2**i for i in range(0, 11)],
    )

    return fig


def add_in_algo_speed_frontier(df, col, x_axis):
    frontier = pareto_frontier(df, x_axis)
    df[col] = [
        frontier.contains(df["algorithm"][idx], idx) for idx in range(len(df[x_axis]))
    ]


def write_output(title: str, outdir: str, df, regenerate: bool):
    os.makedirs(outdir, exist_ok=True)

    add_in_algo_speed_frontier(df, "in_algo_cspeed_frontier", "compress_speed_mbps")
    add_in_algo_speed_frontier(df, "in_algo_dspeed_frontier", "decompress_speed_mbps")
    # filter out openzl points that are not in either frontier
    df = df[
        (df["algorithm"] != "openzl")
        | df["in_algo_cspeed_frontier"]
        | df["in_algo_dspeed_frontier"]
    ]

    df.to_csv(os.path.join(outdir, "data.csv"), index=False)

    algorithm_dfs = {}
    for algo in df["algorithm"].unique():
        algorithm_dfs[algo] = df[df["algorithm"] == algo]

    cspeed_fig = plot(title, algorithm_dfs, "compress_speed_mbps")
    cspeed_fig.write_image(os.path.join(outdir, "cspeed.png"), scale=10)
    cspeed_fig.write_image(os.path.join(outdir, "cspeed.svg"))

    dspeed_fig = plot(title, algorithm_dfs, "decompress_speed_mbps")
    dspeed_fig.write_image(os.path.join(outdir, "dspeed.png"), scale=10)
    dspeed_fig.write_image(os.path.join(outdir, "dspeed.svg"))


def load_csv(csv: str) -> pd.DataFrame:
    df = pd.read_csv(csv)
    if "Compressor name" in df:
        # lzbench
        df["algorithm"] = [n.split(" ")[0] for n in df["Compressor name"]]
        df["config"] = [n.split(" ")[-1] for n in df["Compressor name"]]
        df["ctime"] = df["Original size"] / df["Compression speed"] / 1000000
        df["dtime"] = df["Original size"] / df["Decompression speed"] / 1000000

        df_agg = (
            df.groupby(["algorithm", "config"])
            .agg(
                {
                    "Original size": "sum",
                    "Compressed size": "sum",
                    "ctime": "sum",
                    "dtime": "sum",
                }
            )
            .reset_index()
        )
        df_agg["compression_ratio"] = (
            df_agg["Original size"] / df_agg["Compressed size"]
        )
        df_agg["compress_speed_mbps"] = (
            df_agg["Original size"] / df_agg["ctime"] / 1000000
        )
        df_agg["decompress_speed_mbps"] = (
            df_agg["Original size"] / df_agg["dtime"] / 1000000
        )
        print(len(df_agg))

    elif "ctimeMs" in df:
        # zstrong benchmark

        df_agg = pd.DataFrame(
            {
                "algorithm": ["openzl"],
                "config": [None],
                "srcSize": [(df["iters"] * df["srcSize"]).sum()],
                "compressedSize": [(df["iters"] * df["compressedSize"]).sum()],
                "ctimeMs": [df["ctimeMs"].sum()],
                "dtimeMs": [df["dtimeMs"].sum()],
            }
        )

        df_agg["compression_ratio"] = df_agg["srcSize"] / df_agg["compressedSize"]
        df_agg["compress_speed_mbps"] = df_agg["srcSize"] / df_agg["ctimeMs"] / 1000.0
        df_agg["decompress_speed_mbps"] = df_agg["srcSize"] / df_agg["dtimeMs"] / 1000.0

    return df_agg[
        [
            "algorithm",
            "config",
            "compression_ratio",
            "compress_speed_mbps",
            "decompress_speed_mbps",
        ]
    ]


def load_csvs(csvs: list[str]) -> pd.DataFrame:
    dfs = [load_csv(csv) for csv in csvs]
    return pd.concat(dfs, ignore_index=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--title", type=str, required=True)
    parser.add_argument("--output", type=str, required=True)
    parser.add_argument("--regenerate", action="store_true")
    # Add positional CSV
    parser.add_argument("csvs", nargs="*", type=str)
    args = parser.parse_args()

    if args.regenerate:
        df = pd.read_csv(os.path.join(args.output, "data.csv"))
    else:
        if len(args.csvs) == 0:
            raise Exception("No CSVs provided")
        df = load_csvs(args.csvs)

    os.makedirs(args.output, exist_ok=True)
    write_output(args.title, args.output, df, args.regenerate)


if __name__ == "__main__":
    main()
