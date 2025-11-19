# Real-World Formats

*Chapter 9 - Complete examples*

This chapter presents complete SDDL descriptions for real-world formats, demonstrating how the language features work together in practice.

---

<a id="coverage-map"></a>
## Coverage Map

Use this table to jump from a language concept to the simplest full example that demonstrates it in context.

| Concept | Example |
| --- | --- |
| <a id="coverage-expect"></a>`expect` validation | [SAO Catalog (Introduction)](introduction.md#a-more-complex-example) |
| <a id="coverage-where"></a>`where` inline validation | [Example 1: WAVE PCM](#example-1-wave-pcm-audio-16-bit-mono-stereo) |
| <a id="coverage-arrays-fixed"></a>Fixed-size arrays | [Example 1: WAVE PCM](#example-1-wave-pcm-audio-16-bit-mono-stereo) |
| <a id="coverage-arrays-parameter"></a>Parameterized record arrays | [SAO Catalog (Introduction)](introduction.md#a-more-complex-example) |
| <a id="coverage-arrays-auto"></a>Auto-sized arrays | [SAO simple example (Introduction)](introduction.md#a-short-example) |
| <a id="coverage-var"></a>`var` derived values | [SAO Catalog (Introduction)](introduction.md#a-more-complex-example) |
| <a id="coverage-align-up"></a>`align_up` row padding | [Example 2: BMP 24-bit](#example-2-bmp-image-24-bit-rgb-uncompressed) |
| <a id="coverage-align-directive"></a>`align(n)` directive | Not yet covered – see [Alignment chapter](alignment-padding.md#field-alignment-with-alignn) |
| <a id="coverage-pad-to"></a>`pad_to` sized scopes | [Example 3: WAVE Extended](#example-3-wave-audio-extended-format-support) |
| <a id="coverage-pad-align"></a>`pad_align` record padding | [Example 2: BMP 24-bit](#example-2-bmp-image-24-bit-rgb-uncompressed) |
| <a id="coverage-when-then"></a>`when ... then` conditionals | [SAO Catalog (Introduction)](introduction.md#a-more-complex-example) |
| <a id="coverage-when-block"></a>`when { ... }` conditionals | [SAO Catalog (Introduction)](introduction.md#a-more-complex-example) |
| <a id="coverage-unions"></a>Variant unions & dispatch | [Example 3: WAVE Extended](#example-3-wave-audio-extended-format-support) |
| <a id="coverage-enums"></a>Enums, `in`, switch expressions | [Example 3: WAVE Extended](#example-3-wave-audio-extended-format-support) |
| <a id="coverage-size-checks"></a>`size(field)` function | [Example 2: BMP 24-bit](#example-2-bmp-image-24-bit-rgb-uncompressed) |
| <a id="coverage-sizeof"></a>`sizeof()` static size checks | [SAO Catalog (Introduction)](introduction.md#a-more-complex-example) |
| <a id="coverage-soa"></a>`soa` structure-of-arrays layout | Not yet covered – see [Arrays chapter](arrays-collections.md#structure-of-arrays-layout) |
| <a id="coverage-delimiter"></a>Delimiter-based fields (`Bytes until ...`) | Not yet covered – see [Core Concepts](core-concepts.md#delimiter-based-parsing) |
| <a id="coverage-scan"></a>`scan` keyword for arrays | Not yet covered – see [Arrays chapter](arrays-collections.md#auto-sized-arrays) |
| <a id="coverage-annotations"></a>Annotations (`@instant_parse`, `@err_msg`, etc.) | Not yet covered – see [Annotations section](sddl-for-llm.md#10-annotations) |

---

## Example 1: WAVE PCM Audio (16-bit, Mono/Stereo)

**Format Restrictions:**

This example covers the WAVE subset used most often in practice. Only PCM audio is accepted, with 16-bit samples and either mono or stereo layouts. The format expects the standard 16-byte `fmt` chunk immediately followed by `data`, so files with extended `fmt` payloads, intermediate metadata chunks, or extra data after `data` are rejected. Non-PCM encodings (MP3, μ-law, etc.), alternative bit depths, and layouts with more than two channels are likewise treated as invalid.

**What This Example Teaches:**

The spec shows how to use `where` clauses on each chunk field, compute array lengths from header values, and validate derived quantities such as `byte_rate` and `block_align`. It demonstrates straightforward RIFF parsing: verify the chunk IDs in order, enforce the size expectations, and then read the sample array according to the recorded length.

```sddl
# WAVE PCM 16-bit Mono/Stereo Format

# RIFF Chunk
magic: Bytes(4) where magic == "RIFF"

file_size: UInt32LE  # Size of rest of file

wave_id: Bytes(4) where wave_id == "WAVE"

# Format Chunk
fmt_id: Bytes(4) where fmt_id == "fmt "

fmt_size: UInt32LE where fmt_size == 16  # PCM format chunk is 16 bytes (rejects extended)

audio_format: UInt16LE where audio_format == 1  # 1 = PCM (rejects compressed formats)

num_channels: UInt16LE where num_channels == 1 or num_channels == 2  # Mono or stereo only

sample_rate: UInt32LE  # Any sample rate accepted

byte_rate: UInt32LE where byte_rate == sample_rate * num_channels * 2  # Bytes per second

block_align: UInt16LE where block_align == num_channels * 2  # Bytes per sample frame

bits_per_sample: UInt16LE where bits_per_sample == 16  # 16-bit only

# Data Chunk
data_id: Bytes(4) where data_id == "data"

data_size: UInt32LE

samples: Int16LE[data_size / 2]  # 16-bit = 2 bytes per sample
```

**Validation and Rejection:**

Any violation of the `where` predicates produces a data error: mismatched magic numbers, compressed formats (`audio_format != 1`), unsupported bit depths, channel counts outside {1,2}, or inconsistent `byte_rate`/`block_align`. The parser also fails if the chunk order differs from `RIFF → fmt → data` or if extra data remains after the samples.

---

## Example 2: BMP Image (24-bit RGB, Uncompressed)

**Format Restrictions:**

This BMP description focuses on standard 24-bit RGB images with a BITMAPINFOHEADER. The parser accepts only bottom-up images (positive height), single-plane data, and the canonical 40-byte header. Indexed color depths, compressed variants (RLE4/8), OS/2 headers, and top-down layouts are rejected. Because the spec models scanlines explicitly, it enforces the 4-byte row alignment required by the format and ignores palette metadata.

**What This Example Teaches:**

The example shows how to validate each header field with `where`, describe RGB pixels as their own record, and wrap them in a `Scanline` that uses `pad_align 4` to satisfy row alignment. It also demonstrates shape checks by comparing `image_size` against `size(pixel_rows)` instead of re-deriving padding math.

```sddl
# BMP 24-bit RGB Uncompressed Format

# File Header (14 bytes)
magic: Bytes(2) where magic == "BM"

file_size: UInt32LE

reserved1: UInt16LE
reserved2: UInt16LE

pixel_data_offset: UInt32LE

# Info Header (40 bytes - BITMAPINFOHEADER)
header_size: UInt32LE where header_size == 40  # Standard BITMAPINFOHEADER only

width: Int32LE where width > 0

height: Int32LE where height > 0  # Bottom-up only (top-down uses negative height)

planes: UInt16LE where planes == 1

bits_per_pixel: UInt16LE where bits_per_pixel == 24  # 24-bit RGB only

compression: UInt32LE where compression == 0  # BI_RGB (uncompressed) only

image_size: UInt32LE  # Can be 0 for uncompressed

x_pixels_per_meter: Int32LE
y_pixels_per_meter: Int32LE

colors_used: UInt32LE
colors_important: UInt32LE

# Pixel Data Structures
Record RGB() = {
  blue:  UInt8,
  green: UInt8,
  red:   UInt8
}

Record Scanline(width) = {
  pixels: RGB[width]
} pad_align 4  # Rows padded to 4-byte boundaries

# Pixel Data
pixel_rows: Scanline(width)[height]
expect image_size == 0 or image_size == size(pixel_rows)
```

**Validation and Rejection:**

Files fail parsing when any header predicate is violated (wrong magic, alternative header size, negative dimensions, plane count not equal to 1, wrong bit depth, compression enabled). The spec also raises a data error if the pixel payload length does not match `size(pixel_rows)` or if trailing bytes remain after the rows are parsed.

---

## Example 3: WAVE Audio (Extended Format Support)

**Format Scope:**

This specification targets the broad range of uncompressed WAVE files used in practice: PCM data from 8 to 32 bits, IEEE_FLOAT recordings at 32 or 64 bits, and EXTENSIBLE files whose subformat GUID resolves to PCM or FLOAT. It accepts one to eight channels and sample rates between 8 kHz and 384 kHz. Extended `fmt` payloads are parsed when present, and every `fmt`/`data` chunk is padded to even-byte boundaries.

The parser rejects RF64/RIFX containers, compressed formats, EXTENSIBLE files with unsupported GUIDs, more than eight channels, atypical sample rates, and any file that inserts extra chunks between `fmt` and `data` or repeats either chunk. It also ensures `ValidBitsPerSample` matches `BitsPerSample` in EXTENSIBLE headers.

**Concepts Illustrated:**

This example combines enums and the `in` operator for dispatch, grouped `when { ... }` checks, `pad_to`/`pad_align` for chunk sizing, nested unions for sample data, and switch expressions to resolve EXTENSIBLE GUIDs. It also demonstrates GUID validation, use of `size()` to confirm RIFF container sizes, and layered validation to keep `fmt`, `data`, and derived fields consistent.

```sddl
# WAVE Extended Format - PCM/FLOAT (1-8 channels, 8-384kHz)
# Accepts: PCM (8/16/24/32-bit), IEEE_FLOAT (32/64-bit), EXTENSIBLE (PCM/FLOAT subtypes)
# Rejects: Compressed formats, extra chunks, invalid invariants

# ---------- Enums ----------
enum WaveFormat { PCM = 1, IEEE_FLOAT = 3, EXTENSIBLE = 0xFFFE }

# ---------- Fixed headers ----------
Record RIFF_Header() = {
  ChunkID:   Bytes(4),
  ChunkSize: UInt32LE,                 # bytes after this field (file_size - 8)
  Format:    Bytes(4)
}

Record ChunkHeader() = {
  ID:   Bytes(4),                      # "fmt ", "data"
  Size: UInt32LE                       # payload byte count (excludes pad)
}

# ---------- 'fmt ' payload ----------
Record FmtCore() = {
  AudioFormat:   UInt16LE,             # 1, 3, or 0xFFFE
  NumChannels:   UInt16LE,
  SampleRate:    UInt32LE,
  ByteRate:      UInt32LE,
  BlockAlign:    UInt16LE,
  BitsPerSample: UInt16LE
}

# Extra depends on cbSize → requires scanning
Record FmtExtra(total_size) = {
  cbSize:    UInt16LE,
  expect cbSize == total_size - 16,
  ExtraData: Bytes(cbSize)
}

Record FmtExtensibleTail() = {
  ValidBitsPerSample: UInt16LE,
  ChannelMask:        UInt32LE,
  SubFormat:          Bytes(16)         # GUID
}

# Size-bounded scope via pad_to
Record FmtPayload(total_size) = {
  core: FmtCore,

  # Profile guards
  expect core.AudioFormat in WaveFormat,                                # {1,3,0xFFFE}
  expect between(1, core.NumChannels, 8),
  expect between(8000, core.SampleRate, 384000),
  expect (
      (core.AudioFormat == WaveFormat.PCM        and (core.BitsPerSample == 8 or core.BitsPerSample == 16 or core.BitsPerSample == 24 or core.BitsPerSample == 32))
   or (core.AudioFormat == WaveFormat.IEEE_FLOAT and (core.BitsPerSample == 32 or core.BitsPerSample == 64))
   or (core.AudioFormat == WaveFormat.EXTENSIBLE)
  ),

  # Typical size discipline (fail-fast)
  when core.AudioFormat == WaveFormat.PCM { expect (total_size == 16) },
  when core.AudioFormat == WaveFormat.IEEE_FLOAT { expect (total_size >= 18) },
  when core.AudioFormat == WaveFormat.EXTENSIBLE { expect (total_size >= 40) },

  when total_size > 16 { extra: FmtExtra(total_size) },

  # Extensible constraints (grouped)
  when core.AudioFormat == WaveFormat.EXTENSIBLE {
    ext: FmtExtensibleTail,
    expect ext.ValidBitsPerSample == core.BitsPerSample,
    expect (
      # PCM  GUID {00000001-0000-0010-8000-00AA00389B71}
      ext.SubFormat == [0x01,0x00,0x00,0x00, 0x00,0x00, 0x10,0x00, 0x80,0x00, 0x00,0xAA,0x00,0x38,0x9B,0x71]
      or
      # FLOAT GUID {00000003-0000-0010-8000-00AA00389B71}
      ext.SubFormat == [0x03,0x00,0x00,0x00, 0x00,0x00, 0x10,0x00, 0x80,0x00, 0x00,0xAA,0x00,0x38,0x9B,0x71]
    )
  },

  # Algebraic invariants
  var bytes_per_sample = ceil_div(core.BitsPerSample, 8),
  expect core.BlockAlign == core.NumChannels * bytes_per_sample,
  expect core.ByteRate   == core.SampleRate  * core.BlockAlign
} pad_to total_size

# ---------- Bit-depth sample unions ----------
Union PCM_Sample(bits) = {
  case 8:  UInt8,                       # PCM8 is unsigned in RIFF
  case 16: Int16LE,
  case 24: Bytes(3),                    # 24-bit stored as 3 bytes
  case 32: Int32LE
}

Union Float_Sample(bits) = {
  case 32: Float32LE,
  case 64: Float64LE
}

# ---------- Sample atom (normalized format → bits) ----------
Union SampleAtom(fmt_eff, bits) = {
  case WaveFormat.PCM:        PCM_Sample(bits),
  case WaveFormat.IEEE_FLOAT: Float_Sample(bits)
}

# ---------- Interleaved frame ----------
Record InterleavedFrame(fmt_eff, bits, channels) = {
  ch: SampleAtom(fmt_eff, bits)[channels]
}

# ---------- 'data' payload ----------
Record DataPayload(fmt_eff, bits, channels, block_align, total_bytes) = {
  var bps = ceil_div(bits, 8),
  expect block_align == channels * bps,
  expect total_bytes % block_align == 0,
  var frames = total_bytes / block_align,
  frames_data: InterleavedFrame(fmt_eff, bits, channels)[frames]
}

# ---------- Chunk wrappers (size-bounded + even padding) ----------
Record FmtChunk() = {
  h: ChunkHeader,
  expect h.ID == "fmt ",
  body: FmtPayload(h.Size)
} pad_align 2

Record DataChunk(fmt_eff, bits, channels, block_align) = {
  h: ChunkHeader,
  expect h.ID == "data",
  body: DataPayload(fmt_eff, bits, channels, block_align, h.Size)
} pad_align 2

# ==========================================
# ROOT LAYOUT
# ==========================================
riff: RIFF_Header
expect riff.ChunkID == "RIFF"
expect riff.Format  == "WAVE"

fmt:  FmtChunk

# Map EXTENSIBLE GUID to format tag (for PCM or FLOAT subtypes)
# Uses boolean-to-integer trick: (condition) evaluates to 0 or 1, multiply by desired tag
# If PCM GUID matches: 1*1=1; if FLOAT GUID matches: 1*3=3; if neither: 0+0=0
var subfmt_tag =
  (fmt.body.ext.SubFormat == [0x01,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71]) * 1 +
  (fmt.body.ext.SubFormat == [0x03,0x00,0x00,0x00,0x00,0x00,0x10,0x00,0x80,0x00,0x00,0xAA,0x00,0x38,0x9B,0x71]) * 3

var fmt_eff =
  switch fmt.body.core.AudioFormat {
    case WaveFormat.PCM:        WaveFormat.PCM,
    case WaveFormat.IEEE_FLOAT: WaveFormat.IEEE_FLOAT,
    case WaveFormat.EXTENSIBLE:
      switch subfmt_tag {
        case 1: WaveFormat.PCM,
        case 3: WaveFormat.IEEE_FLOAT,
      },
  }

data: DataChunk(fmt_eff,
                fmt.body.core.BitsPerSample,
                fmt.body.core.NumChannels,
                fmt.body.core.BlockAlign)

# RIFF size must account for the 4-byte "WAVE" + both chunks (incl. padding)
expect riff.ChunkSize == 4 + size(fmt) + size(data)

```

---

## Common Patterns From These Examples

### Magic Number Validation

All three examples validate format signatures (Examples 1, 2, 3):

```sddl
magic: Bytes(4)
expect magic == "RIFF"
```

### Derived Size Calculations

Computing array sizes from header fields (Examples 1, 2, 3):

```sddl
data_size: UInt32LE
samples: Int16LE[data_size / 2]
```

### Field Validation

Using `expect` to validate invariants (Examples 1, 2, 3):

```sddl
block_align: UInt16LE
expect block_align == num_channels * 2
```

### Alignment with `align_up`

Row or block padding to boundaries (Example 2):

```sddl
var row_size_padded = align_up(row_size_unpadded, 4)
```

### Unions for Variants

Dispatching on format types (Example 3):

```sddl
Union PCM_Sample(bits) = {
  case 8:  UInt8,
  case 16: Int16LE,
  case 24: Bytes(3),
  case 32: Int32LE
}
```

### Size-Bounded Records with `pad_to`

Enforcing exact chunk sizes (Example 3):

```sddl
Record FmtPayload(total_size) = {
  # ... fields ...
} pad_to total_size
```

### Chunk Padding with `pad_align`

Even-byte boundaries for RIFF chunks (Example 3):

```sddl
Record FmtChunk() = {
  # ... fields ...
} pad_align 2
```

---

## Summary

These three examples show SDDL's core features in action:

- **Example 1** demonstrates basic validation and size calculations
- **Example 2** adds alignment and padding for image rows
- **Example 3** shows advanced features: enums, unions, GUIDs, and complex validation

**Key Principles:**

- Always validate magic numbers and critical fields
- Use `var` for derived calculations
- Explicitly specify endianness (LE/BE)
- Start simple and add complexity incrementally
- Test with real files to validate your description

---

## Where to Go Next

- **[Reference](reference.md)** for a concise syntax summary while writing specs.
- Revisit the chapter that matches the feature you need (arrays, conditionals, etc.) and compare with these examples.
- Validate your own SDDL files against real data to confirm the structure.
