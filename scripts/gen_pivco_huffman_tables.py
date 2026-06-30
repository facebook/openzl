#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.

from __future__ import annotations

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SCRIPT = "scripts/gen_pivco_huffman_tables.py"
GENERATED = "generated"


def header_guard(path: str) -> str:
    return path.replace("src/", "").replace("/", "_").replace(".", "_").upper()


def hex8(value: int) -> str:
    assert 0 <= value <= 0xFF
    return f"0x{value:02x}"


def hex64(value: int) -> str:
    assert 0 <= value <= 0xFFFFFFFFFFFFFFFF
    return f"0x{value:016x}ULL"


def pack_le64(values: list[int]) -> int:
    assert len(values) == 8
    out = 0
    for i, value in enumerate(values):
        out |= value << (8 * i)
    return out


def repeat_byte(value: int) -> int:
    assert 0 <= value <= 0xFF
    return value * 0x0101010101010101


def popcounts() -> list[int]:
    return [mask.bit_count() for mask in range(256)]


def zeros_counts() -> list[int]:
    return [8 - mask.bit_count() for mask in range(256)]


def compress_u16(mask: int, pad: int) -> list[int]:
    out = []
    for lane in range(8):
        if (mask & (1 << lane)) != 0:
            out += [2 * lane, 2 * lane + 1]
    out += [pad] * (16 - len(out))
    return out


def compress_u16_pair(pad: int) -> list[list[int]]:
    return [
        compress_u16(mask, pad) + compress_u16((~mask) & 0xFF, pad)
        for mask in range(256)
    ]


def partition_lo16(mask: int) -> list[int]:
    # Low-half gather control for the 16-wide partition: right ranks (set lanes)
    # packed forward at the front, left ranks (unset lanes) packed reversed at the
    # far back; the gap in between is filled by the high-half control.
    out = [0x80] * 16
    pos = 0
    for lane in range(8):
        if (mask >> lane) & 1:
            out[pos] = lane
            pos += 1
    pos = 15
    for lane in range(8):
        if not ((mask >> lane) & 1):
            out[pos] = lane
            pos -= 1
    return out


def partition_hi_base16(mask: int) -> list[int]:
    # High-half gather control before zero-padding: right ranks packed forward at
    # the front, left ranks packed reversed ending at lane 7. Values are source
    # byte indices (8 + lane), since the high half lives in bytes 8..15.
    out = [0x80] * 16
    pos = 0
    for lane in range(8):
        if (mask >> lane) & 1:
            out[pos] = 8 + lane
            pos += 1
    pos = 7
    for lane in range(8):
        if not ((mask >> lane) & 1):
            out[pos] = 8 + lane
            pos -= 1
    return out


def partition_lo() -> list[list[int]]:
    return [partition_lo16(mask) for mask in range(256)]


def partition_hi_padded() -> list[list[int]]:
    # Pad each high-half control with 8 zero (0x80) bytes on either end so a
    # misaligned load at offset (8 - popcount(loMask)) slides the high contribution
    # into the correct output lanes.
    pad = [0x80] * 8
    return [pad + partition_hi_base16(mask) + pad for mask in range(256)]


def rhs_select8(mask: int) -> list[int]:
    rhs = 0
    out = []
    for bit in range(8):
        if (mask & (1 << bit)) != 0:
            out.append(rhs)
            rhs += 1
        else:
            out.append(0x80)
    return out


def lhs_select8(mask: int) -> list[int]:
    lhs = 0
    out = []
    for bit in range(8):
        if (mask & (1 << bit)) == 0:
            out.append(lhs)
            lhs += 1
        else:
            out.append(0x80)
    return out


def select8x2() -> list[list[int]]:
    return [rhs_select8(mask) + lhs_select8(mask) for mask in range(256)]


def byte_splatx2() -> list[list[int]]:
    return [[mask.bit_count()] * 8 + [8 - mask.bit_count()] * 8 for mask in range(256)]


def select8_u64() -> list[int]:
    return [pack_le64(rhs_select8(mask)) for mask in range(256)]


def byte_splat_u64() -> list[int]:
    return [repeat_byte(mask.bit_count()) for mask in range(256)]


def neon_expand8(mask: int, lhs_base: int, rhs_base: int) -> list[int]:
    lhs = lhs_base
    rhs = rhs_base
    out = []
    for bit in range(8):
        if (mask & (1 << bit)) != 0:
            out.append(rhs)
            rhs += 1
        else:
            out.append(lhs)
            lhs += 1
    return out


def neon_expand8_table(rhs_base: int) -> list[list[int]]:
    return [neon_expand8(mask, lhs_base=0, rhs_base=rhs_base) for mask in range(256)]


def neon_expand8_pre() -> list[list[list[int]]]:
    return [
        [
            neon_expand8(mask, lhs_base=8 - prior_ones, rhs_base=16 + prior_ones)
            for mask in range(256)
        ]
        for prior_ones in range(9)
    ]


def avx512_unpack_permute(depth: int) -> list[int]:
    out = []
    for group in range(8):
        for i in range(8):
            out.append(group * depth + i if i < depth else 0)
    return out


def avx512_shift_control(depth: int) -> list[int]:
    return [i * depth for _group in range(8) for i in range(8)]


def avx512_unpack_permute_tables() -> list[list[int]]:
    return [avx512_unpack_permute(depth) for depth in range(2, 8)]


def avx512_shift_control_tables() -> list[list[int]]:
    return [avx512_shift_control(depth) for depth in range(2, 8)]


def format_u8_values(values: list[int], per_line: int = 16) -> str:
    lines = []
    for i in range(0, len(values), per_line):
        lines.append(
            "    " + ", ".join(str(value) for value in values[i : i + per_line]) + ","
        )
    return "\n".join(lines)


def format_u8_table_1d(name: str, values: list[int], align: int) -> str:
    return (
        f"static const ZL_ALIGNED({align}) uint8_t {name}[{len(values)}] = {{\n"
        f"{format_u8_values(values)}\n"
        "};"
    )


def format_u8_row(row: list[int]) -> str:
    return "{ " + ", ".join(hex8(value) for value in row) + " }"


def format_u8_table_2d(name: str, rows: list[list[int]], align: int) -> str:
    row_len = len(rows[0])
    body = ",\n".join("    " + format_u8_row(row) for row in rows)
    return (
        f"static const ZL_ALIGNED({align}) uint8_t {name}[{len(rows)}][{row_len}] = {{\n"
        f"{body}\n"
        "};"
    )


def format_u8_table_3d(name: str, planes: list[list[list[int]]], align: int) -> str:
    plane_len = len(planes[0])
    row_len = len(planes[0][0])
    formatted_planes = []
    for plane in planes:
        rows = ",\n".join("        " + format_u8_row(row) for row in plane)
        formatted_planes.append(f"    {{\n{rows}\n    }}")
    body = ",\n".join(formatted_planes)
    return (
        f"static const ZL_ALIGNED({align}) uint8_t "
        f"{name}[{len(planes)}][{plane_len}][{row_len}] = {{\n"
        f"{body},\n"
        "};"
    )


def format_u64_table(name: str, values: list[int], align: int) -> str:
    lines = []
    for i in range(0, len(values), 4):
        line = ", ".join(hex64(value) for value in values[i : i + 4])
        lines.append(f"    {line},")
    body = "\n".join(lines)
    return (
        f"static const ZL_ALIGNED({align}) uint64_t {name}[{len(values)}] = {{\n"
        f"{body}\n"
        "};"
    )


def render_header(path: str, body: str) -> str:
    guard = header_guard(path)
    return f"""// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef {guard}
#define {guard}

/**
 * @{GENERATED} by {SCRIPT}
 * Please don't modify this file directly!
 */

#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

// clang-format off
{body}
// clang-format on

ZL_END_C_DECLS

#endif // {guard}
"""


def render_x86_tables() -> str:
    body = "\n\n".join(
        [
            format_u8_table_2d("ZL_kPivCoHuffmanSelect8x2", select8x2(), align=16),
            format_u8_table_2d("ZL_kPivCoHuffmanByteSplatx2", byte_splatx2(), align=16),
            format_u8_table_1d("ZL_kPivCoHuffmanPopcount8", popcounts(), align=16),
        ]
    )
    return render_header(
        "src/openzl/codecs/pivco_huffman/arch/common_pivco_x86_tables.h",
        body,
    )


def render_x86_merge_tables() -> str:
    body = "\n\n".join(
        [
            format_u64_table("ZL_kPivCoHuffmanSelect8", select8_u64(), align=16),
            format_u64_table("ZL_kPivCoHuffmanByteSplat", byte_splat_u64(), align=16),
        ]
    )
    return render_header(
        "src/openzl/codecs/pivco_huffman/arch/common_pivco_x86_merge_tables.h",
        body,
    )


def render_x86_partition_tables() -> str:
    body = "\n\n".join(
        [
            format_u8_table_2d(
                "ZL_kPivCoHuffmanCompressU16Pair", compress_u16_pair(0x80), align=32
            ),
            format_u8_table_2d("ZL_kPivCoHuffmanPartitionLo", partition_lo(), align=16),
            format_u8_table_2d(
                "ZL_kPivCoHuffmanPartitionHiPadded",
                partition_hi_padded(),
                align=32,
            ),
        ]
    )
    return render_header(
        "src/openzl/codecs/pivco_huffman/arch/common_pivco_x86_partition_tables.h",
        body,
    )


def render_neon_tables() -> str:
    body = "\n\n".join(
        [
            format_u8_table_1d("ZL_kPivCoHuffmanNeonPopcount", popcounts(), align=64),
            format_u8_table_1d(
                "ZL_kPivCoHuffmanNeonZeroCount", zeros_counts(), align=64
            ),
            format_u8_table_2d(
                "ZL_kPivCoHuffmanNeonCompressU16",
                compress_u16_pair(0xFF),
                align=32,
            ),
            format_u8_table_2d(
                "ZL_kPivCoHuffmanNeonExpand8",
                neon_expand8_table(rhs_base=8),
                align=32,
            ),
            format_u8_table_2d(
                "ZL_kPivCoHuffmanNeonExpand8Q",
                neon_expand8_table(rhs_base=16),
                align=32,
            ),
            format_u8_table_3d(
                "ZL_kPivCoHuffmanNeonExpand8Pre",
                neon_expand8_pre(),
                align=64,
            ),
        ]
    )
    return render_header(
        "src/openzl/codecs/pivco_huffman/arch/common_pivco_neon_tables.h",
        body,
    )


def render_avx512_tables() -> str:
    body = "\n\n".join(
        [
            format_u8_table_2d(
                "ZL_kPivCoHuffmanAvx512UnpackPermute",
                avx512_unpack_permute_tables(),
                align=64,
            ),
            format_u8_table_2d(
                "ZL_kPivCoHuffmanAvx512ShiftControl",
                avx512_shift_control_tables(),
                align=64,
            ),
        ]
    )
    return render_header(
        "src/openzl/codecs/pivco_huffman/arch/common_pivco_avx512_tables.h",
        body,
    )


def main() -> None:
    outputs = {
        "src/openzl/codecs/pivco_huffman/arch/common_pivco_x86_tables.h": render_x86_tables(),
        "src/openzl/codecs/pivco_huffman/arch/common_pivco_x86_merge_tables.h": render_x86_merge_tables(),
        "src/openzl/codecs/pivco_huffman/arch/common_pivco_x86_partition_tables.h": render_x86_partition_tables(),
        "src/openzl/codecs/pivco_huffman/arch/common_pivco_neon_tables.h": render_neon_tables(),
        "src/openzl/codecs/pivco_huffman/arch/common_pivco_avx512_tables.h": render_avx512_tables(),
    }
    for path, contents in outputs.items():
        (ROOT / path).write_text(contents)
        print(f"generated {path}")


if __name__ == "__main__":
    main()
