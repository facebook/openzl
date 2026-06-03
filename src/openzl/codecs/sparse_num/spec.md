## Sparse Num Decoder Specification

### Inputs

The decoder for the `sparse_num` transform takes two numeric streams as input:

1. `distances` -- dominant-symbol run lengths.
2. `values` -- literal values.

The `values` stream element width determines the output element width. It must be
1, 2, 4, or 8 bytes. The dominant symbol and all literal values use this element
width.

Literal values may be equal to the dominant symbol. This is valid and
unambiguous because each distance is interpreted relative to the next literal
value, not relative to the next non-dominant value.

The `distances` stream element width determines the maximum representable
dominant-symbol run. It must be 1, 2, or 4 bytes. Distance values are interpreted
as unsigned integers in the stream's element width. The decoder accepts any
supported distance width, even when a narrower width would be sufficient to
represent all distances.

The number of distance elements must be equal to the number of literal value
elements, or exactly one greater than the number of literal value elements. No
other count relationship is valid.

### Codec Header

The codec header selects the dominant symbol.

- If the codec header is empty, the dominant symbol is numeric value 0. The
  `values` stream contains only literal values.
- If the codec header is non-empty, its bytes are the dominant symbol encoded in
  little-endian byte order. The header size must be no greater than the `values`
  stream element width. Any missing high bytes are implicit zero. The `values`
  stream contains only literal values.

For example, dominant symbol `1` is encoded as `01`, dominant symbol `256` is
encoded as `00 01`, and dominant symbol `0x123456` is encoded as `56 34 12`.

### Decoding Algorithm

Each distance is the number of dominant-symbol elements before a boundary. A
boundary is either the next literal value or the end of the reconstructed stream.

Let `numLiterals` be the number of elements in `values`, and `numDistances` be
the number of elements in `distances`.

- If `numDistances == numLiterals`, the reconstructed stream ends with the last
  literal value.
- If `numDistances == numLiterals + 1`, the final distance is the
  dominant-symbol run to the end of the reconstructed stream. A final distance of
  zero is valid and means the stream ends immediately after the last literal
  value.

The decoder computes the number of output elements as:

```
sum(distances) + numLiterals
```

The decoder then produces output as follows:

1. Decode the dominant symbol from the codec header.
2. For each literal index `i` in `[0, numLiterals)`:
   1. Emit `distances[i]` copies of the dominant symbol.
   2. Emit `values[i]`.
3. If `numDistances == numLiterals + 1`, emit `distances[numLiterals]` copies of
   the dominant symbol.

The output size computation must be checked for integer overflow before
allocating the output stream. Decoding must fail if the distance/value count
relationship is invalid, if the codec header is larger than the value element
width, if any stream has an unsupported element width, or if the computed output
size cannot be represented.

### Encoder Guidance

This specification defines decoder behavior. Encoders are expected to choose an
efficient, canonical representation, but the decoder must accept less efficient
representations when they are unambiguous and otherwise valid.

Encoders should use the narrowest distance width that can represent all emitted
distances. Decoders must still accept any supported distance width.

Encoders should normally omit literals equal to the dominant symbol and represent
dominant-symbol values through distances. Encoders may still emit literals equal
to the dominant symbol when doing so is useful, for example to split an otherwise
unrepresentable dominant-symbol run while keeping distances limited to 32 bits.

Encoders should use the shorter representation when the input ends with a
literal value, omitting the final zero-length distance. Decoders must still
accept `numDistances == numLiterals + 1` with a final zero distance.

Encoders should use the canonical dominant-symbol header:

- If the dominant symbol is 0, use an empty codec header.
- Otherwise, encode the dominant symbol in little-endian byte order, using the
  shortest non-empty byte sequence that represents it. The `values` stream
  contains only literal values.

Decoders must still accept unambiguous non-canonical representations, such as a
non-empty codec header with trailing zero bytes.

- Empty input is encoded as no distances and no literal values.
- An all-dominant-symbol input of length `N > 0` is encoded as one distance with
  value `N` and no literal values, when `N` fits in the selected distance width.
- Longer dominant-symbol runs may be split by emitting a dominant-symbol literal
  after the largest representable distance.
- An input ending in a literal value has `numDistances == numLiterals`.
- An input ending in one or more dominant-symbol values has
  `numDistances == numLiterals + 1`.

### Output

The decoder produces one numeric stream. Its element width is the element width
of `values`, and its element count is `sum(distances) + numLiterals`.
