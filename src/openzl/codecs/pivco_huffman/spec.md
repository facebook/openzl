## PivCo-Huffman Decoder Specification

### Overview

`pivco_huffman` is a static, byte-oriented Huffman codec. Rather than decoding
one codeword at a time, *pivco* coding decodes a whole block of symbols at
once: it walks the Huffman tree breadth-first and, at each internal node, splits
the block's symbols into the two children according to a single codeword bit. The
split decisions are stored as bitmaps, which the decoder replays to scatter
symbols into place.

The Huffman tree is never serialized. Both sides rebuild an identical tree
deterministically from the transmitted weights (see *Tree construction*), so the
bitstream carries only the per-node partition bitmaps and a small count per node.

The codec is endian-independent: every multi-byte quantity on the wire is either
an varint or a little-endian bit packing, both defined below.

### Inputs

The decoder takes two streams:

1. `weights` — a numeric stream of 1-byte zstd-style Huffman weights.
2. `bitstream` — the pivco-coded payload (a serial byte stream).

It produces one serial stream of `decoded_size` bytes.

### Codec header

The codec header is carried out-of-band from `bitstream`. Multi-byte integers are
varints.

```
decoded_size : varint
block_size   : varint        (optional)
... trailing bytes ignored
```

- `decoded_size` is the number of output bytes.
- `block_size` is the number of output bytes per pivco block. It is **optional**:
  it is present exactly when the encoder split the input into more than one block.
  Its presence is detected by whether any header bytes remain after `decoded_size`
  has been parsed:
  - If absent, `block_size` defaults to `decoded_size` (the whole output is one
    block). In which case `decoded_size` must be no greater than `ZL_PIVCO_MAX_BLOCK_SIZE` (2^30).
  - If present, it must be non-zero, and the decoder rejects it if it exceeds
    `ZL_PIVCO_MAX_BLOCK_SIZE` (2^30). A `block_size` that does not match the one
    the encoder used yields corrupt output (caught as a parse failure or by the
    frame's integrity check).
- Any bytes after the fields the decoder understands are **ignored**. This is
  reserved for future, backward-compatible extensions (for example, a jump table
  of per-block bitstream offsets to enable parallel decoding).

### weights stream

The `weights` stream length is the number of weight entries and must be no greater
than 256. Weight entry `i` describes byte symbol `i`; symbols beyond the last
entry are implicitly absent.

Each weight must be in the range 0..12. A zero weight means the symbol is absent
from the alphabet. A non-zero weight follows the zstd convention: a symbol with
code length `L` under a table of `tableLog` bits has weight `tableLog + 1 - L`, so
a larger weight means a shorter code.

The non-zero weights must describe a complete prefix code. Equivalently,

```
sum over non-zero weights of (1 << (weight - 1))
```

must be a non-zero power of two; `tableLog` is its base-2 logarithm and must be no
greater than 12.

`decoded_size == 0` is encoded with an empty `weights` stream and an empty
`bitstream`; the two emptiness conditions must agree (`decoded_size == 0` iff
`weights` is empty), otherwise the frame is corrupt.

### Tree construction

The tree is a function of `weights` alone. An independent decoder must reproduce
the construction below exactly, because the resulting structure (in particular the
flat-leaf grouping) determines the bitstream layout. Encoder and decoder run the
identical deterministic builder.

1. **Validate and derive `tableLog`** from the weights, as in *weights stream*
   above.

2. **Ranks (canonical order).** Order all present (non-zero weight) symbols by
   *descending weight* (shortest code first); break ties by *ascending symbol
   value*. A symbol's **rank** is its index in this order. `numRanks` is the count
   of present symbols. PivCo coding operates on contiguous rank ranges.

3. **Canonical codewords.** Visiting ranks in order, assign canonical Huffman
   codewords: start at 0, add 1 per leaf, and shift left by 1 each time the code
   length increases by one level. Each codeword is considered left-justified
   (most-significant bit first). "Level `L`" denotes the `(L+1)`-th bit from the
   most-significant end of the codeword; level 0 is the root.

4. **Flat leaves.** A run of `2^k` symbols that share one code length forms a
   complete depth-`k` subtree. The codec collapses such a run into a single
   **flat leaf** holding `2^k` symbols, decoded by a `k`-bit table lookup instead
   of `k` binary splits. The grouping rule (which both sides must follow
   identically):

   Process weight buckets from shallowest (largest weight) to deepest (smallest
   weight). Within a bucket, while more than two single-symbol leaves remain,
   peel the largest power-of-two block (`2^k`, `k = floor(log2(remaining))`) off
   the **end** of the bucket — i.e. the highest symbol values — into a flat leaf
   whose weight becomes `weight + k` (a shallower level). A residual of one or two
   leaves is left as ordinary single-symbol leaves, since a flat node could not
   improve on a single binary split. The shallowest-first order matters: a peeled
   flat leaf lands in a shallower (larger-weight) bucket, which has already been
   processed, so it is never itself re-flattened.

   Rank and codeword assignment then visits levels shallowest-first; within a
   level it takes the remaining single-symbol leaves (ascending symbol value)
   followed by the flat leaves collapsed into that level. A flat leaf occupies
   `2^k` consecutive ranks, one per contained symbol, and a flat leaf's symbols
   occupy a contiguous slice of the rank order.

A single-symbol leaf has flat depth 0 and is also called a **constant** leaf: it
contributes no bits to the bitstream. The whole-alphabet edge case of one present
symbol produces a tree of one rank, one level, and a constant root.

### Bitstream model

`bitstream` is read as bits packed **little-endian, least-significant-bit first**:
bit index `j` lives in byte `j / 8` at bit position `j % 8` (bit 0 is the LSB).
Integers wider than one bit are packed low bits first. The decoder maintains a bit
cursor and uses two read operations:

- **alignedBitmap(n):** first advance the cursor to the next byte boundary,
  discarding 0–7 padding bits; then take the next `ceil(n / 8)` bytes as an
  `n`-bit bitmap and advance the cursor by exactly `n` bits. Bitmaps therefore
  always start on a byte boundary, but the cursor may end mid-byte.
- **readInt(n):** read the next `n` bits (low bits first) as an unsigned integer,
  with no alignment. `n == 0` reads nothing and yields 0.

When decoding finishes, the number of bytes consumed — the final partial byte
counted as whole — must equal the `bitstream` length exactly. Any leftover or
missing byte is corruption.

### Decoding algorithm

```
for off = 0; off < decoded_size; off += block_size:
    blockLen = min(block_size, decoded_size - off)
    decodeNode(level = 0, range = [0, numRanks), count = blockLen)
        -> writes blockLen bytes to output[off ..]
```

`decodeNode(level, [firstRank, rankEnd), count)` produces `count` output bytes:

**Leaf node** — `[firstRank, rankEnd)` is exactly one leaf, i.e. its flat depth
`d` satisfies `2^d == rankEnd - firstRank`:

- `d == 0` (constant leaf): emit `count` copies of the leaf's single symbol. Read
  nothing.
- `d > 0` (flat leaf): `alignedBitmap(count * d)`. For each output position
  `i` in `[0, count)`, the `d` bits at `[i*d, (i+1)*d)` (low bit first) form an
  index `idx` in `[0, 2^d)`; emit the leaf's `idx`-th symbol, which is
  `rankToSymbol[firstRank + idx]`.

**Internal node** — otherwise:

1. If `level >= numLevels`, the range cannot split: corruption.
2. Compute `splitRank` from the tree: the boundary in `[firstRank, rankEnd)`
   between codewords whose level-`L` bit is 0 (the lower ranks, the **left**
   child) and 1 (the higher ranks, the **right** child). This comes entirely from
   the tree (the weights), never from the bitstream.
3. From the tree, determine whether the left child `[firstRank, splitRank)` and
   the right child `[splitRank, rankEnd)` are each a constant leaf.
4. `alignedBitmap(count)` — the **partition bitmap**. Bit `i` (low bit first)
   selects, for output position `i`, the right child (1) or the left child (0).
5. If **not** both children are constant, read the right child's size:
   `numOnes = readInt(nextPow2(count + 1))`, where `nextPow2(x)` is the number of
   bits needed to represent values `0..x-1` (i.e. `ceil(log2(x))`). Reject if
   `numOnes > count`. `numZeros = count - numOnes`. `numOnes` must equal the
   number of set bits in the partition bitmap, otherwise the frame is corrupt.
   If both children are constant, no integer is read; the partition bitmap alone
   reconstructs the output.
6. Recurse, **left child first**: `decodeNode(level+1, [firstRank, splitRank),
   numZeros)` then `decodeNode(level+1, [splitRank, rankEnd), numOnes)`. The left
   subtree is fully decoded before the right (a pre-order traversal of the tree).
7. **Merge** into this node's output: for each position `i`, take the next byte
   from the right child if bit `i` is set, else from the left child, consuming
   each child's bytes in order. A constant child supplies its single symbol for
   each of its positions; a non-constant child supplies its decoded bytes.

Because a node emits its own bitmap and count before recursing, and recurses left
before right, the bitstream is exactly a pre-order serialization of the tree:
`[node bitmap][node count?] [left subtree...] [right subtree...]`, with each
bitmap re-aligned to a byte boundary and each count packed immediately after its
bitmap.

### Outputs

The decoder produces one serial stream of `decoded_size` byte elements.

### Encoder guidance

Encoders should emit the shortest `weights` stream that includes every non-zero
symbol weight; symbols above `weights.size() - 1` are implicitly absent. The block
size written into the header must match the one used to encode, and is omitted
(defaulting to `decoded_size`) when the input is a single block.

For an empty input, encoders must emit `decoded_size == 0`, an empty `weights`
stream, and an empty `bitstream`. For a constant input, encoders should emit a
single non-zero weight of 1 for the repeated symbol and an empty `bitstream` (the
constant root emits no bits).
