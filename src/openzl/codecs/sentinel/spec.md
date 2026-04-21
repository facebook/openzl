# Sentinel Codec — Decoder Wire Format Specification

## Overview

The sentinel codec replaces values at designated positions with a sentinel
marker and moves the original values to a separate exceptions stream. On the
decoder side the sentinel marker signals that the next value should be read
from the exceptions stream instead of being taken from the values stream.

Multiple encoders share this single decoder.

## Inputs

| Index | Name       | Type    | Description |
|-------|------------|---------|-------------|
| 0     | values     | Numeric | One element per original input element. Elements at exception positions contain the sentinel value; all other elements are the original values (potentially narrowed to a smaller width). Width W_v. |
| 1     | exceptions | Numeric | The original values for positions that were replaced by the sentinel. Width W_e, where W_e equals the width of the original unencoded input. |

### Constraints

* W_v <= W_e (the values stream may be narrower than the exceptions stream,
  but not wider).
* Each element in the values stream is interpreted as an unsigned integer of
  width W_v.
* Each element in the exceptions stream is interpreted as an unsigned integer
  of width W_e.

## Output

| Name   | Type    | Description |
|--------|---------|-------------|
| output | Numeric | The reconstructed original input. Width W_e, element count equals the element count of the values stream. |

## Codec Header

| Condition                        | Header contents  |
|----------------------------------|------------------|
| sentinel == 2^(8 * W_v) - 1      | Empty (0 bytes)  |
| sentinel != 2^(8 * W_v) - 1      | varint(sentinel) |

When the sentinel value is `2^(8 * W_v) - 1` (i.e. all bits set), no header bytes
are needed because this is the default. Otherwise the sentinel value is
encoded as a single varint.

## Decoding Algorithm

```

W_v = element_width(values)
W_e = element_width(exceptions)
N   = element_count(values)
M   = element_count(exceptions)

sentinel_v = 2^(8 * W_v) - 1                # default
if codec_header is not empty:
    sentinel_v = varint_decode(codec_header)
    VALIDATE sentinel_v < 2^(8 * W_v)

VALIDATE W_v <= W_e

allocate output[N] at width W_e

exception_index = 0
for i in 0 .. N-1:
    v = read_element(values, i, W_v)       # unsigned, W_v bytes
    if v == sentinel_v:
        VALIDATE exception_index < M
        output[i] = read_element(exceptions, exception_index, W_e)
        exception_index += 1
    else:
        output[i] = zero_extend(v, W_v, W_e)

VALIDATE exception_index == M              # all exceptions consumed

return output
```

### Security Notes (malicious input handling)

* If `W_v > W_e`, return a corruption error.
* If a sentinel match is encountered but `exception_index >= M`, return a
  corruption error (not enough exceptions).
* If `exception_index != M` after processing all values, return a corruption
  error (unconsumed exceptions).
* If the codec header is non-empty but fails varint decoding, return a
  corruption error.
