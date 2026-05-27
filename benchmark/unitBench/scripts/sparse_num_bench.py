#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.

# pyre-unsafe

from __future__ import annotations

import argparse
import csv
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Sequence


DEFAULT_WORK_DIR = Path("/tmp/openzl_sparse_num_bench")
DEFAULT_BUCK_TARGET = "fbcode//openzl/dev/benchmark/unitBench:unitBench"
DEFAULT_RATIOS = (75, 90, 98)
DEFAULT_WIDTHS = (8, 16, 32, 64)
PERIOD = 100


@dataclass(frozen=True)
class UnitBenchResult:
    source_size: int
    result_size: int
    time_us: float


@dataclass(frozen=True)
class SparseNumCase:
    width_bits: int
    zero_ratio: int
    raw_path: Path
    decode_path: Path
    raw_size: int
    encoded_size: int
    num_elements: int


@dataclass(frozen=True)
class ReportRow:
    case: SparseNumCase
    encode: UnitBenchResult
    decode: UnitBenchResult


def parse_csv_ints(value: str, allowed: set[int], name: str) -> list[int]:
    results: list[int] = []
    for raw_part in value.split(","):
        part = raw_part.strip()
        if not part:
            continue
        try:
            parsed = int(part)
        except ValueError as error:
            raise argparse.ArgumentTypeError(f"{name} must be integers") from error
        if parsed not in allowed:
            choices = ", ".join(str(choice) for choice in sorted(allowed))
            raise argparse.ArgumentTypeError(
                f"unsupported {name} {parsed}; expected one of {choices}"
            )
        results.append(parsed)
    if not results:
        raise argparse.ArgumentTypeError(f"{name} must not be empty")
    return results


def parse_widths(value: str) -> list[int]:
    return parse_csv_ints(value, set(DEFAULT_WIDTHS), "width")


def parse_ratios(value: str) -> list[int]:
    return parse_csv_ints(value, set(DEFAULT_RATIOS), "zero ratio")


def fbcode_root() -> Path | None:
    for parent in Path(__file__).resolve().parents:
        if (parent / "security/frameworks/python/exec/subprocess.py").exists():
            return parent
    return None


def run_command(
    executable: str,
    args: Sequence[str],
    cwd: Path,
) -> subprocess.CompletedProcess[str]:
    root = fbcode_root()
    if root is not None:
        root_str = str(root)
        if root_str not in sys.path:
            sys.path.insert(0, root_str)
    try:
        from security.frameworks.python.exec.subprocess import TrustedSubprocessWithList
    except ImportError:
        pass
    else:
        return TrustedSubprocessWithList.run(
            executable=executable,
            cmd_args=list(args),
            cwd=cwd,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )

    return subprocess.run(
        [executable, *args],
        cwd=cwd,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )


def check_run(
    executable: str,
    args: Sequence[str],
    cwd: Path,
) -> subprocess.CompletedProcess[str]:
    result = run_command(executable, args, cwd)
    if result.returncode != 0:
        command = " ".join([executable, *args])
        raise RuntimeError(
            f"command failed: {command}\nstdout:\n{result.stdout}\nstderr:\n{result.stderr}"
        )
    return result


def build_unitbench(cwd: Path, target: str) -> Path:
    if fbcode_root() is None:
        raise RuntimeError("--unitbench-bin is required when running outside fbcode")
    result = check_run(
        "buck",
        [
            "build",
            "@//mode/opt",
            target,
            "--show-full-simple-output",
        ],
        cwd,
    )
    lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
    if not lines:
        raise RuntimeError("buck build did not print a unitBench binary path")
    return Path(lines[-1])


def resolve_unitbench_bin(path: Path) -> Path:
    resolved = path.expanduser().resolve()
    if not resolved.is_file():
        raise RuntimeError(f"unitBench binary does not exist: {resolved}")
    return resolved


def element_count_for_size(sample_bytes: int, width_bytes: int) -> int:
    num_elements = sample_bytes // width_bytes
    num_elements -= num_elements % PERIOD
    if num_elements <= 0:
        raise RuntimeError("sample size is too small for the selected widths")
    return num_elements


def nonzero_value(index: int, width_bytes: int) -> bytes:
    value = (index % 251) + 1
    return value.to_bytes(width_bytes, "little")


def generate_raw_sample(
    path: Path,
    width_bits: int,
    zero_ratio: int,
    sample_bytes: int,
) -> tuple[int, int]:
    width_bytes = width_bits // 8
    num_elements = element_count_for_size(sample_bytes, width_bytes)
    data = bytearray(num_elements * width_bytes)
    for index in range(num_elements):
        if index % PERIOD < zero_ratio:
            continue
        start = index * width_bytes
        data[start : start + width_bytes] = nonzero_value(index, width_bytes)
    path.write_bytes(data)
    return num_elements, len(data)


def is_zero_value(data: bytes | bytearray, offset: int, width_bytes: int) -> bool:
    return all(byte == 0 for byte in data[offset : offset + width_bytes])


def generate_decode_fixture(raw_path: Path, fixture_path: Path, width_bits: int) -> int:
    width_bytes = width_bits // 8
    raw = raw_path.read_bytes()
    distances = bytearray()
    values = bytearray()
    zero_run = 0
    for offset in range(0, len(raw), width_bytes):
        if is_zero_value(raw, offset, width_bytes):
            zero_run += 1
            if zero_run > 255:
                raise RuntimeError(
                    f"{raw_path} needs distance width > 8 for generated fixture"
                )
            continue
        distances.append(zero_run)
        values.extend(raw[offset : offset + width_bytes])
        zero_run = 0
    if zero_run != 0:
        raise RuntimeError(f"{raw_path} ends with zeros; d8 fixture would need a tail")
    # Keep the values stream first so unitBench hands the decode kernel an
    # aligned numeric values buffer. D8 distances do not need extra alignment.
    fixture_path.write_bytes(values + distances)
    return len(distances) + len(values)


def prepare_case(
    work_dir: Path,
    width_bits: int,
    zero_ratio: int,
    sample_bytes: int,
) -> SparseNumCase:
    raw_path = work_dir / f"sparse_num_u{width_bits}_zero{zero_ratio}.raw"
    decode_path = work_dir / f"sparse_num_u{width_bits}_zero{zero_ratio}.d8"
    num_elements, raw_size = generate_raw_sample(
        raw_path,
        width_bits,
        zero_ratio,
        sample_bytes,
    )
    encoded_size = generate_decode_fixture(raw_path, decode_path, width_bits)
    return SparseNumCase(
        width_bits=width_bits,
        zero_ratio=zero_ratio,
        raw_path=raw_path,
        decode_path=decode_path,
        raw_size=raw_size,
        encoded_size=encoded_size,
        num_elements=num_elements,
    )


def parse_unitbench_csv(stdout: str, benchmark_name: str) -> UnitBenchResult:
    for line in reversed(stdout.splitlines()):
        if "," not in line:
            continue
        row = next(csv.reader([line]))
        if len(row) < 5 or row[2].strip() != benchmark_name:
            continue
        return UnitBenchResult(
            source_size=int(row[1].strip()),
            result_size=int(row[3].strip()),
            time_us=float(row[4].strip()),
        )
    raise RuntimeError(f"could not parse unitBench CSV output for {benchmark_name}")


def run_unitbench(
    unitbench_bin: Path,
    benchmark_name: str,
    sample_path: Path,
    duration_s: int,
    cwd: Path,
) -> UnitBenchResult:
    result = check_run(
        str(unitbench_bin),
        [
            f"-i={duration_s}",
            "--csv",
            benchmark_name,
            str(sample_path),
        ],
        cwd,
    )
    return parse_unitbench_csv(result.stdout, benchmark_name)


def mib_per_second(num_bytes: int, time_us: float) -> float:
    if time_us <= 0:
        return 0.0
    return (num_bytes / (1024 * 1024)) / (time_us / 1_000_000)


def format_size(num_bytes: int) -> str:
    if num_bytes >= 1024 * 1024:
        return f"{num_bytes / (1024 * 1024):.2f} MiB"
    if num_bytes >= 1024:
        return f"{num_bytes / 1024:.2f} KiB"
    return f"{num_bytes} B"


def format_table(rows: Sequence[ReportRow]) -> str:
    lines = [
        "| width | zeros | elements | raw | encoded | ratio | encode MiB/s | decode MiB/s |",
        "|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for row in rows:
        case = row.case
        ratio = case.raw_size / case.encoded_size if case.encoded_size else 0.0
        encode_speed = mib_per_second(case.raw_size, row.encode.time_us)
        decode_speed = mib_per_second(row.decode.result_size, row.decode.time_us)
        lines.append(
            f"| u{case.width_bits} | {case.zero_ratio}% | {case.num_elements} | "
            f"{format_size(case.raw_size)} | {format_size(case.encoded_size)} | "
            f"{ratio:.2f} | {encode_speed:.1f} | {decode_speed:.1f} |"
        )
    return "\n".join(lines)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate sparse_num samples, run unitBench, and print markdown results."
    )
    parser.add_argument(
        "--work-dir",
        type=Path,
        default=DEFAULT_WORK_DIR,
        help=f"Directory for generated samples. Default: {DEFAULT_WORK_DIR}",
    )
    parser.add_argument(
        "--sample-mib",
        type=int,
        default=16,
        help="Raw sample size per width/ratio, in MiB. Default: 16",
    )
    parser.add_argument(
        "--duration-s",
        type=int,
        default=1,
        help="unitBench duration per benchmark, in seconds. Default: 1",
    )
    parser.add_argument(
        "--ratios",
        type=parse_ratios,
        default=list(DEFAULT_RATIOS),
        help="Comma-separated zero ratios. Default: 75,90,98",
    )
    parser.add_argument(
        "--widths",
        type=parse_widths,
        default=list(DEFAULT_WIDTHS),
        help="Comma-separated value widths. Default: 8,16,32,64",
    )
    parser.add_argument(
        "--buck-target",
        default=DEFAULT_BUCK_TARGET,
        help=f"unitBench Buck target. Default: {DEFAULT_BUCK_TARGET}",
    )
    parser.add_argument(
        "--unitbench-bin",
        type=Path,
        default=None,
        help=(
            "Use an already-built unitBench binary instead of building with Buck. "
            "Required when running outside fbcode."
        ),
    )
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    repo_dir = Path.cwd()
    args.work_dir.mkdir(parents=True, exist_ok=True)
    sample_bytes = args.sample_mib * 1024 * 1024
    unitbench_bin = (
        resolve_unitbench_bin(args.unitbench_bin)
        if args.unitbench_bin is not None
        else build_unitbench(repo_dir, args.buck_target)
    )

    rows: list[ReportRow] = []
    for width_bits in args.widths:
        for zero_ratio in args.ratios:
            case = prepare_case(args.work_dir, width_bits, zero_ratio, sample_bytes)
            encode = run_unitbench(
                unitbench_bin,
                f"sparseNumEncode{width_bits}",
                case.raw_path,
                args.duration_s,
                repo_dir,
            )
            decode = run_unitbench(
                unitbench_bin,
                f"sparseNumDecode{width_bits}_d8",
                case.decode_path,
                args.duration_s,
                repo_dir,
            )
            if encode.result_size != case.encoded_size:
                raise RuntimeError(
                    f"encode size mismatch for {case.raw_path}: "
                    f"unitBench={encode.result_size}, fixture={case.encoded_size}"
                )
            if decode.result_size != case.raw_size:
                raise RuntimeError(
                    f"decode size mismatch for {case.decode_path}: "
                    f"unitBench={decode.result_size}, raw={case.raw_size}"
                )
            rows.append(ReportRow(case=case, encode=encode, decode=decode))

    print(format_table(rows))


if __name__ == "__main__":
    main()
