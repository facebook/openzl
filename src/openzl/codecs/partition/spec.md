## Partition Decoder Specification
### Inputs
The decoder for the 'partition' transform takes two streams as input:
- a numeric stream of 8-bit unsigned bucket IDs (one per element).
- a serial stream containing a packed bitstream of intra-bucket offsets.

### Codec Header
The 'partition' codec header encodes the partition parameters: the element width of the decoded stream, a start value, and the size of each partition. The partitions define contiguous, non-overlapping ranges of unsigned integers. The maximum number of partitions is 256.

#### Flags Byte
The first byte of the header is always present and encodes flags that determine the header format.

| Bits | Meaning |
|------|---------|
| `[1:0]` | `log2(elementWidth)`: 0 = 1 byte, 1 = 2 bytes, 2 = 4 bytes, 3 = 8 bytes |
| `[2]` | IS_PRESET: parameters are a built-in preset; remaining bits encode the preset ID |
| `[3]` | IS_FIRST_VALUE_ZERO: `startValue` is 0 and omitted from the header |
| `[4]` | Unused |
| `[5]` | IS_POW2: all partition sizes are powers of 2 (compact encoding) |
| `[7:6]` | When IS_POW2 is set: `numBits - 3`, where `numBits` is the bit-width used to encode each log2(partitionSize) value |

#### Preset Mode (bit 2 set)
When the IS_PRESET bit is set, the header is exactly 1 byte. The preset ID is `flags >> 3`. The following presets are defined:

| Preset ID | Name | Partitions | Start Value | Description |
|-----------|------|------------|-------------|-------------|
| 0 | Quantize Offsets | 32 | 1 | Power-of-2 sizes: 1, 2, 4, ..., 2^31. Covers [1, 2^32). |
| 1 | Quantize Lengths | 44 | 0 | First 16 partitions each have size 1 (values 0-15). Then power-of-2 sizes: 16, 32, ..., 2^31. Covers [0, 2^32). |
| 2 | Varbyte16 | 16 | 0 | Sizes: 2, 2, 4, 8, 16, ..., 2^15. Covers [0, 2^16). |

#### Power-of-2 Mode (bit 5 set, bit 2 clear)
When IS_POW2 is set but IS_PRESET is not, the partition sizes are all powers of 2 and are encoded compactly as their log2 values in a bitstream.

The header layout is: `[flags] [varint startValue?] [bitstream of log2(sizes)]`.

The `startValue` is present as a varint unless IS_FIRST_VALUE_ZERO is set. The varint format is described here: https://protobuf.dev/programming-guides/encoding/#varints.

The bitstream encodes each `log2(partitionSize)` value using `numBits` bits, where `numBits = ((flags >> 6) & 3) + 3`. A sentinel `1` bit follows the last value. The number of partitions is determined by dividing the total number of payload bits (excluding the sentinel) by `numBits`.

#### General Mode (bits 2 and 5 clear)
When neither IS_PRESET nor IS_POW2 is set, partition sizes are stored as consecutive varints.

The header layout is: `[flags] [varint startValue?] [varint size_0] [varint size_1] ... [varint size_{N-1}]`.

The `startValue` is present as a varint unless IS_FIRST_VALUE_ZERO is set. The number of partitions is determined by the number of varints that fit in the remaining header bytes.

### Decoding
The partition parameters define `N` contiguous ranges of unsigned integers. The base value for each partition is computed as:
- `bases[0] = startValue`
- `bases[i] = bases[i-1] + partitionSizes[i-1]`

The number of extra bits for each partition is computed as:
- `bits[i] = ceil(log2(partitionSizes[i]))`

For each element, the decoder reads the bucket ID from the first input stream and `bits[bucket]` bits from the second input stream (the offset bitstream). The decoded value is `bases[bucket] + offset`.

Consider the partition parameters `startValue = 10`, `partitionSizes = {4, 8, 4}`. This defines three partitions covering [10, 14), [14, 22), and [22, 26). The computed base values are `{10, 14, 22}` and the extra bits per partition are `{2, 3, 2}`.

Given the bucket stream {0, 1, 2, 1} and extra bits bitstream containing the offsets {2, 3, 1, 0} encoded using {2, 3, 2, 3} bits respectively, the decoder produces:
- Element 0: `bases[0] + 2 = 10 + 2 = 12`
- Element 1: `bases[1] + 3 = 14 + 3 = 17`
- Element 2: `bases[2] + 1 = 22 + 1 = 23`
- Element 3: `bases[1] + 0 = 14 + 0 = 14`

The decoded stream is {12, 17, 23, 14}.

### Outputs
The output of the decoder is a single numeric stream. The element width is encoded in the codec header flags byte and must be 1, 2, 4, or 8 bytes. The number of elements is equal to the number of elements in the bucket ID input stream.
