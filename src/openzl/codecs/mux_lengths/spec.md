## Mux Lengths Decoder Specification

### Terminology

This codec refers to the two regenerated streams as `literal_lengths` and
`match_lengths`, as the intended use case for this codec is to encode the literal
and match lengths produced by LZ compression. This is not the only possible use case,
and it may be used beyond this purpose. However, for clarity and simplicity, this
document will refer to the two streams as literal and match lengths.

### Inputs

The decoder for the 'mux_lengths' transform takes two inputs:

1. `muxed_lengths` — a serial stream of muxed byte values, one per length pair.
2. `long_lengths` — a numeric stream of overflow length values (interleaved
   literal and match length overflows).

### Codec Header

The codec header is exactly 1 byte with the following layout:

| Bits  | Field              | Description                                      |
|-------|--------------------|--------------------------------------------------|
| [0:3] | `split_point`      | Number of bits in each muxed byte for the literal length (range 0–8) |
| [4:7] | `match_length_bias` | Bias subtracted (wrapping unsigned) before muxing match lengths (range 0–15) |

- `split_point = header & 0x0F`
- `match_length_bias = header >> 4`

Values of `split_point` greater than 8 are invalid and indicate a corrupt frame.

Note: `split_point` values of `0` and `8` are both valid, and indicate that the literal
lengths or match lengths respectively are always pushed to the `long_lengths`
stream.

### Decoding Algorithm

Derived values:
- `ll_mask = (1 << split_point) - 1`
- `ml_max = match_length_bias + (1 << (8 - split_point)) - 1`

For each byte `mux` in the `muxed_lengths` stream:

1. Extract inline literal length: `ll = mux & ll_mask`
2. Extract inline match length: `ml = match_length_bias + (mux >> split_point)`
3. If `ll == ll_mask`, read the next value from `long_lengths` and add it to `ll` with wrapping unsigned arithmetic.
4. If `ml == ml_max`, read the next value from `long_lengths` and add it to `ml` with wrapping unsigned arithmetic.
5. Emit `ll` to `literal_lengths` and `ml` to `match_lengths`.

Note: when both literal and match lengths overflow for the same element, the
literal length overflow is stored first in the `long_lengths` stream.

Note: `ml` values less than `match_length_bias` are encoded as `ml - match_length_bias` using wrapping
unsigned arithmetic, which maps them to the top `match_length_bias` values of the unsigned range.

Note: There are multiple ways to encode values `[0, ll_mask)` and `[match_length_bias, ml_max)`.
They could be encoded using only the mux byte, or they could be encoded with wrapping
arithmetic using large values of the long lengths stream. It likely doesn't make sense to choose
the larger encoding, but it is not a problem.

After processing all muxed bytes, the `long_lengths` stream must be fully
consumed. If it has unconsumed elements, the frame is corrupt.

### Outputs

The decoder produces two numeric streams, both with element width `W` (matching
the element width of the `long_lengths` input) and element count equal to the
number of bytes in the `muxed_lengths` stream:

1. `literal_lengths` — the decoded literal lengths.
2. `match_lengths` — the decoded match lengths.
