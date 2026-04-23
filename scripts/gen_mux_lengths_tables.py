#!/usr/bin/env python3
# Copyright (c) Meta Platforms, Inc. and affiliates.


def build_compress_u16_shuffle_lut() -> list[list[int]]:
    lut = []
    for byte in range(256):
        shuffle = []
        for i in range(8):
            if byte & (1 << i):
                shuffle.append(2 * i)
                shuffle.append(2 * i + 1)
        shuffle += [0x80] * (16 - len(shuffle))
        lut.append(shuffle)
    return lut


def build_expand_u16_shuffle_lut() -> list[list[int]]:
    lut = []
    for byte in range(256):
        shuffle = []
        pos = 0
        for i in range(8):
            if byte & (1 << i):
                shuffle.append(2 * pos)
                shuffle.append(2 * pos + 1)
                pos += 1
            else:
                shuffle.append(0x80)
                shuffle.append(0x80)
        lut.append(shuffle)
    return lut


def header_guard(path):
    return path.replace("src/", "").replace("/", "_").replace(".", "_").upper()


def hexify(num):
    assert num < 256
    h = hex(num)
    if len(h) == 3:
        return "0x0" + h[-1]
    else:
        return h


def print_shuffle(shuffle):
    return f"{{ {', '.join(hexify(x) for x in shuffle)} }}"


SHUFFLES_TEMPLATE = """static const ZL_ALIGNED({shuffle_len}) uint8_t {name}[{num_shuffles}][{shuffle_len}] = {{
    {shuffles}
}};"""


def print_shuffle_lut(name, shuffles):
    return SHUFFLES_TEMPLATE.format(
        name=name,
        num_shuffles=len(shuffles),
        shuffle_len=len(shuffles[0]),
        shuffles=",\n    ".join(print_shuffle(r) for r in shuffles),
    )


PATH = "src/openzl/codecs/mux_lengths/common_mux_lengths_luts.h"
HEADER_GUARD_MACRO = header_guard(PATH)

# Make a f-string so the script doesn't get marked as generated
GENERATED = "generated"

TEMPLATE = f"""// Copyright (c) Meta Platforms, Inc. and affiliates.
#ifndef {HEADER_GUARD_MACRO}
#define {HEADER_GUARD_MACRO}

/**
 * @{GENERATED} by scripts/gen_mux_lengths_tables.py
 * Please don't modify this file directly!
 */

#include "openzl/shared/portability.h"

ZL_BEGIN_C_DECLS

// clang-format off
/**
 * Compresses uint16_t values at positions selected by set bits in an 8-bit mask
 * and packs them contiguously to the front. Each of the 256 rows is a 16-byte
 * PSHUFB control vector.
 *
 * For output position i in [0, 8]:
 *   i < popcount(mask) -> select input at the index of the i-th set bit
 *   i >= popcount(mask) -> zero (0x80)
 */
{print_shuffle_lut("ZL_kCompressU16LUT", build_compress_u16_shuffle_lut())}

/**
 * Expands contiguously packed uint16_t values into positions selected by an
 * 8-bit mask. Each of the 256 rows is a 16-byte PSHUFB control vector.
 *
 * For output position i in [0, 8]:
 *   bit i SET   -> select the next packed input uint16_t
 *   bit i CLEAR -> zero (PSHUFB treats index 0x80 as zero)
 */
{print_shuffle_lut("ZL_kExpandU16LUT", build_expand_u16_shuffle_lut())}
// clang-format on

ZL_END_C_DECLS

#endif // {HEADER_GUARD_MACRO}
"""

with open(PATH, "w") as f:
    f.write(TEMPLATE)

print(TEMPLATE)
