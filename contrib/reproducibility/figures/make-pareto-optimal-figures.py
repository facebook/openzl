#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.

import argparse
import os
import random
import shutil
import sys
from pathlib import Path
from subprocess import check_output


random.seed(0xDEADBEEF)


def run(cmd, *args, **kwargs) -> None:
    print(f"Running: {' '.join(cmd)}")
    return check_output(cmd, *args, **kwargs)


def hg_root():
    return run(["hg", "root"]).decode("utf-8").strip()


def buck_build(target: str, output: str, save: bool = True) -> None:
    openzl_dir = Path(__file__).parents[3]
    if save:
        run(
            [
                "buck",
                "build",
                "@fbcode//mode/opt",
                target,
                "--out",
                output,
            ],
            cwd=openzl_dir,
        )
    else:
        out = run(
            ["buck", "build", "@fbcode//mode/opt", target, "--show-output"],
            cwd=openzl_dir,
        ).decode("utf-8")
        return os.path.join(hg_root(), out.split(" ")[1].strip())
    return output


def copy_subset(files: [str], output: str, n: int | None) -> None:
    shutil.rmtree(output, ignore_errors=True)
    os.makedirs(output)

    subset = random.sample(files, min(n, len(files))) if n is not None else files

    with open(f"{output}.txt", "w") as f:
        for idx, file in enumerate(subset):
            shutil.copy(file, os.path.join(output, f"{idx}"))
            f.write(f"{file}\n")

    return subset


def partition_samples(
    dataset: str, output: str, num_train_files: int | None, num_test_files: int | None
) -> None:
    all_files = []
    if os.path.isdir(dataset):
        for root, _, files in os.walk(dataset):
            all_files.extend([os.path.join(root, file) for file in files])
    else:
        all_files = [dataset]

    train_files = copy_subset(
        all_files, os.path.join(output, "samples/train"), num_train_files
    )

    if len(train_files) < len(all_files):
        all_files = [file for file in all_files if file not in train_files]

    copy_subset(all_files, os.path.join(output, "samples/test"), num_test_files)


def run_lzbench(lzbench: str, output: str) -> None:
    print("Running lzbench...")
    with open(os.path.join(output, "lzbench.csv"), "w") as f:
        results = run(
            [
                lzbench,
                "-ezstd,1,3,5,7,9,11,13,15,16,19/xz,1,3,5,7,9/zlib,1,3,5,7,9",
                "-o4",
                "-r",
                os.path.join(output, "samples/test"),
            ],
        )
        f.write(results.decode("utf-8"))


def run_zli_training(
    zli: str, profile: str, profile_arg: str | None, output: str
) -> None:
    print("Running training...")

    compressors = os.path.join(output, "zli-train")
    shutil.rmtree(compressors, ignore_errors=True)
    run(
        [
            zli,
            "train",
            "--profile",
            profile,
            os.path.join(output, "samples/train"),
            "--output",
            compressors,
            "--pareto-frontier",
        ]
        + (["--profile-arg", profile_arg] if profile_arg is not None else [])
    )


def run_zli_benchmark(zli: str, output: str) -> None:
    print("Running benchmarking...")
    compressor_dir = os.path.join(output, "zli-train")
    benchmark_dir = os.path.join(output, "zli-benchmark")
    shutil.rmtree(benchmark_dir, ignore_errors=True)
    os.makedirs(benchmark_dir)

    for compressor in os.listdir(compressor_dir):
        if not compressor.endswith(".zc"):
            continue

        run(
            [
                zli,
                "benchmark",
                "-c",
                os.path.join(compressor_dir, compressor),
                os.path.join(output, "samples/test"),
                "--output-csv",
                os.path.join(benchmark_dir, compressor[:-3] + ".csv"),
            ]
        )


def run_tradeoff_plots(tradeoff_plots, output: str, title: str) -> None:
    print("Running tradeoff plots...")

    benchmark_dir = os.path.join(output, "zli-benchmark")
    benchmarks = [
        os.path.join(benchmark_dir, benchmark)
        for benchmark in os.listdir(benchmark_dir)
    ]

    run(
        [
            tradeoff_plots,
            "--title",
            title,
            "--output",
            os.path.join(output, "plots"),
            os.path.join(output, "lzbench.csv"),
        ]
        + benchmarks
    )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("dataset", type=str)
    parser.add_argument("--title", type=str, required=True)
    parser.add_argument("--lzbench", type=str, required=True)
    parser.add_argument("--zli", type=str)
    parser.add_argument("--tradeoff-plots", type=str)
    parser.add_argument("--output", type=str, required=True)
    parser.add_argument("--profile", type=str, required=True)
    parser.add_argument("--profile-arg", type=str, default=None)
    parser.add_argument("--num-train-files", type=int)
    parser.add_argument("--num-test-files", type=int)

    args = parser.parse_args()

    os.makedirs(os.path.join(args.output, "bin"), exist_ok=True)
    shutil.copy(args.lzbench, os.path.join(args.output, "bin"))
    if args.zli is None:
        args.zli = buck_build("cli:zli", os.path.join(args.output, "bin", "zli"))
    else:
        shutil.copy(args.zli, os.path.join(args.output, "bin"))
    if args.tradeoff_plots is None:
        args.tradeoff_plots = buck_build(
            "contrib/reproducibility/figures:tradeoff-plots[inplace]",
            os.path.join(args.output, "bin", "tradeoff-plots"),
            save=False,
        )
    else:
        shutil.copy(args.tradeoff_plots, os.path.join(args.output, "bin"))
    with open(os.path.join(args.output, "cmdline.txt"), "w") as f:
        f.write(" ".join(sys.argv))

    partition_samples(
        args.dataset, args.output, args.num_train_files, args.num_test_files
    )
    run_zli_training(args.zli, args.profile, args.profile_arg, args.output)
    run_zli_benchmark(args.zli, args.output)
    run_lzbench(args.lzbench, args.output)
    run_tradeoff_plots(args.tradeoff_plots, args.output, args.title)


if __name__ == "__main__":
    main()
