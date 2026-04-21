# LZ Codec

## Overview

The LZ codec implements LZ77 matching on a byte-level serial input stream,
decomposing it into four output streams: literals, offsets, literal lengths, and
match lengths.

## Inputs

| Stream | Type   | Element Width | Description              |
|--------|--------|---------------|--------------------------|
| input  | Serial | 1             | Input byte stream        |

## Outputs

| Index | Stream          | Type    | Element Width | Description                                   |
|-------|-----------------|---------|---------------|-----------------------------------------------|
| 0     | literals        | Serial  | 1             | Literal bytes not part of matches             |
| 1     | offsets         | Numeric | Any           | Distance back to start of match               |
| 2     | literal_lengths | Numeric | Any           | Number of literal bytes before each match     |
| 3     | match_lengths   | Numeric | Any           | Number of bytes to copy from the match        |

The decoder MUST accept any element width for the three numeric streams.
However, the decoder MAY only optimize for certain widths, e.g. 2-byte and 4-byte.

## Codec Header

A single varint encoding the decompressed (original) size in bytes.

## Decoding Algorithm

```python
def decode(literals, offsets, literal_lengths, match_lengths):
    assert len(offsets) == len(literal_lengths) == len(match_lengths)

    output = []
    lit_pos = 0
    for offset, lit_len, match_len in zip(offsets, literal_lengths, match_lengths):
        # Copy literals
        assert lit_pos + lit_len <= len(literals)
        output.extend(literals[lit_pos : lit_pos + lit_len])
        lit_pos += lit_len

        # Copy match
        # NOTE: match_len may be larger than offset
        assert offset != 0
        assert offset <= len(output)
        for i in range(match_len):
            output.append(output[-offset])

    # Copy remaining literals
    output.extend(literals[lit_pos:])

    return output
```
