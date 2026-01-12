# Copyright (c) Meta Platforms, Inc. and affiliates.

import functools
import io
import os
import random
import shutil
from typing import List

import click
import numpy as np
from manifold.clients.python import ManifoldClient


# Read 100MB of data_compression_corpora/tree/enwik9 from manifold
@functools.cache
def read_manifold(bucket: str, path: str, size: int = 1024 * 1024 * 100) -> bytes:
    # pyre-ignore
    with ManifoldClient(bucket) as client:
        stream = io.BytesIO()
        client.sync_get(path, stream)
        return stream.getvalue()[:size]


def read_enwik9(size: int = 100 * 1024 * 1024) -> bytes:
    return read_manifold("data_compression_corpora", "tree/enwik9", size)


def generate_enwik_samples(n: int, size: int = 1024) -> List[np.array]:
    data = read_enwik9(size * n * 8)
    assert len(data) >= size * n * 8
    buffer = np.frombuffer(data, dtype=np.int64)
    return list(buffer.reshape((-1, size)))


def generate_delta_samples(n: int, size: int = 1024) -> List[np.array]:
    return [
        random.randrange(1 << random.randrange(63))
        + (np.array(range(size), dtype=np.int64) * random.randrange(1000))
        for _ in range(n)
    ]


def generate_tokenize_samples(n: int, size: int = 1024) -> List[np.array]:
    return [
        np.random.choice(
            np.random.randint(1 << np.random.randint(63))
            + np.random.randint(1 << 10, size=size // 20, dtype=np.int64),
            size=size,
            replace=True,
        )
        for _ in range(n)
    ]


def generate_dispatch_sample(size: int = 1024) -> np.array:
    arrs = []
    for _ in range(size // 16):
        bits = random.randrange(63)
        arrs.append(np.random.randint(1 << bits, size=16, dtype=np.int64))
    return np.append([], arrs)


def generate_dispatch_samples(n: int, size: int = 1024) -> List[np.array]:
    return [generate_dispatch_sample(size) for _ in range(n)]


def generate_samples() -> List[np.array]:
    samples = (
        generate_delta_samples(1000)
        + generate_tokenize_samples(1000)
        + generate_dispatch_samples(1000)
    )
    # samples += generate_enwik_samples(1000)
    np.random.shuffle(samples)
    return samples


def save_samples(path: str, samples: List[np.array]):
    """
    Re-creates the directory in %path and saves new samples to it
    """
    try:
        shutil.rmtree(path)
    except Exception:
        pass
    os.mkdir(path)
    for i, s in enumerate(samples):
        open(os.path.join(path, str(i)), "wb").write(s.tobytes())


@click.command()
@click.argument("output-path", type=click.Path(file_okay=False))
@click.option("--enwik/--no-enwik", default=False)
def main(output_path: str, enwik: bool):
    """
    Generate samples and save them to the output path, removes all pre-existing files in output path
    """
    samples = generate_samples()
    if enwik:
        samples += generate_enwik_samples(1000)
    save_samples(output_path, samples)


if __name__ == "__main__":
    main()
