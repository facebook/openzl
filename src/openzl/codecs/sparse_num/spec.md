## Sparse Num Decoder Specification

### Inputs

The decoder for the `sparse_num` transform takes two numeric streams as input:

1. `distances` -- zero-run lengths.
2. `values` -- literal values.

The `values` stream element width determines the output element width. It must be
1, 2, 4, or 8 bytes.

Literal values may be zero. This is valid and unambiguous because each distance
is interpreted relative to the next literal value, not relative to the next
nonzero value.

The `distances` stream element width determines the maximum representable zero
run. It must be 1, 2, or 4 bytes. Distance values are interpreted as unsigned
integers in the stream's element width. The decoder accepts any supported
distance width, even when a narrower width would be sufficient to represent all
distances.

The number of distance elements must be equal to the number of value elements, or
exactly one greater than the number of value elements. No other count
relationship is valid.

### Codec Header

The codec header is empty.

### Decoding Algorithm

Each distance is the number of zero elements before a boundary. A boundary is
either the next literal value or the end of the reconstructed stream.

Let `numValues` be the number of elements in `values`, and `numDistances` be the
number of elements in `distances`.

- If `numDistances == numValues`, the reconstructed stream ends with the last
  value.
- If `numDistances == numValues + 1`, the final distance is the zero run to the
  end of the reconstructed stream. A final distance of zero is valid and means
  the stream ends immediately after the last value.

The decoder computes the number of output elements as:

```
sum(distances) + numValues
```

The decoder then produces output as follows:

1. For each value index `i` in `[0, numValues)`:
   1. Emit `distances[i]` zero elements.
   2. Emit `values[i]`.
2. If `numDistances == numValues + 1`, emit `distances[numValues]` zero
   elements.

The output size computation must be checked for integer overflow before
allocating the output stream. Decoding must fail if the distance/value count
relationship is invalid, if any stream has an unsupported element width, or if
the computed output size cannot be represented.

### Encoder Guidance

This specification defines decoder behavior. Encoders are expected to choose an
efficient, canonical representation, but the decoder must accept less efficient
representations when they are unambiguous and otherwise valid.

Encoders should use the narrowest distance width that can represent all emitted
distances. Decoders must still accept any supported distance width.

Encoders should normally omit zero literals and represent zero values through
distances. Encoders may still emit zero literals when doing so is useful, for
example to split an otherwise unrepresentable zero run while keeping distances
limited to 32 bits.

Encoders should use the shorter representation when the input ends with a
literal value, omitting the final zero-length distance. Decoders must still
accept `numDistances == numValues + 1` with a final zero distance.

- Empty input is encoded as no distances and no values.
- An all-zero input of length `N > 0` is encoded as one distance with value `N`
  and no values, when `N` fits in the selected distance width.
- Longer zero runs may be split by emitting a zero literal after the largest
  representable distance.
- An input ending in a literal value has `numDistances == numValues`.
- An input ending in one or more zero values has `numDistances == numValues + 1`.

### Output

The decoder produces one numeric stream. Its element width is the element width
of `values`, and its element count is `sum(distances) + numValues`.
