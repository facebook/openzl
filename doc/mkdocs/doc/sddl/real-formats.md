# Real-World Formats

*Chapter 9 - Complete examples*

This chapter presents complete SDDL descriptions for real-world formats, demonstrating how the language features work together in practice.

---

## Example 1: WAVE PCM Audio (16-bit, Mono/Stereo)

**Format Restrictions:**

This example describes a subset of the WAVE format with these constraints:

**Supported:**

- PCM (uncompressed) audio only
- 16-bit samples only
- Mono (1 channel) or Stereo (2 channels) only
- Standard 16-byte fmt chunk
- Immediate data chunk after fmt (no other chunks in between)

**Not Supported (will be rejected):**

- Compressed formats (MP3, ADPCM, μ-law, A-law, etc.)
- Other bit depths (8-bit, 24-bit, 32-bit float)
- Multi-channel/surround (3+ channels)
- Extended fmt chunks (size > 16 bytes)
- Additional chunks between fmt and data (LIST, INFO, etc.)
- Extra chunks after data chunk

**What This Example Teaches:**

- Using `expect` statements to validate field values
- Computing array sizes from header fields (`data_size / 2`)
- Validating derived values (`byte_rate`, `block_align`)
- Sequential chunk parsing (RIFF container structure)
- Rejecting non-conforming files explicitly

```sddl
# WAVE PCM 16-bit Mono/Stereo Format

# RIFF Chunk
magic: Bytes(4)
expect magic == "RIFF"

file_size: UInt32LE  # Size of rest of file

wave_id: Bytes(4)
expect wave_id == "WAVE"

# Format Chunk
fmt_id: Bytes(4)
expect fmt_id == "fmt "

fmt_size: UInt32LE
expect fmt_size == 16  # PCM format chunk is 16 bytes (rejects extended)

audio_format: UInt16LE
expect audio_format == 1  # 1 = PCM (rejects compressed formats)

num_channels: UInt16LE
expect num_channels == 1 or num_channels == 2  # Mono or stereo only

sample_rate: UInt32LE  # Any sample rate accepted

byte_rate: UInt32LE
expect byte_rate == sample_rate * num_channels * 2  # Bytes per second

block_align: UInt16LE
expect block_align == num_channels * 2  # Bytes per sample frame

bits_per_sample: UInt16LE
expect bits_per_sample == 16  # 16-bit only

# Data Chunk
data_id: Bytes(4)
expect data_id == "data"

data_size: UInt32LE

samples: Int16LE[data_size / 2]  # 16-bit = 2 bytes per sample
```

**What gets rejected (data errors):**

- Files with bit depths other than 16-bit: `expect bits_per_sample == 16`
- Compressed audio (MP3, ADPCM, etc.): `expect audio_format == 1`
- Multi-channel audio (3+ channels): `expect num_channels == 1 or num_channels == 2`
- Incorrect byte_rate: `expect byte_rate == sample_rate * num_channels * 2`
- Incorrect block_align: `expect block_align == num_channels * 2`
- Wrong chunk ordering (e.g., metadata between fmt and data): `expect data_id == "data"`
- Extra chunks after the data chunk (unconsumed data causes error)

---

## Example 2: BMP Image (24-bit RGB, Uncompressed)

**Format Restrictions:**

This example describes a subset of the BMP format with these constraints:

**Supported:**

- 24-bit RGB uncompressed only
- Standard 40-byte BITMAPINFOHEADER
- Bottom-up pixel ordering only (positive height)
- Single image plane

**Not Supported (will be rejected):**

- Other bit depths (1-bit, 4-bit, 8-bit indexed, 16-bit, 32-bit)
- Compressed formats (RLE4, RLE8, etc.)
- Other header types (OS/2 BMP, BITMAPV4HEADER, BITMAPV5HEADER)
- Top-down images (negative height)
- Color palettes
- Multiple planes

**What This Example Teaches:**

- Computing array sizes with alignment (`align_up` for 4-byte row padding)
- Using `var` statements for derived calculations
- Validating multiple header fields
- Handling row padding in image formats

```sddl
# BMP 24-bit RGB Uncompressed Format

# File Header (14 bytes)
magic: Bytes(2)
expect magic == "BM"

file_size: UInt32LE

reserved1: UInt16LE
reserved2: UInt16LE

pixel_data_offset: UInt32LE

# Info Header (40 bytes - BITMAPINFOHEADER)
header_size: UInt32LE
expect header_size == 40  # Standard BITMAPINFOHEADER only

width: Int32LE
expect width > 0

height: Int32LE
expect height > 0  # Bottom-up only (top-down uses negative height)

planes: UInt16LE
expect planes == 1

bits_per_pixel: UInt16LE
expect bits_per_pixel == 24  # 24-bit RGB only

compression: UInt32LE
expect compression == 0  # BI_RGB (uncompressed) only

image_size: UInt32LE  # Can be 0 for uncompressed

x_pixels_per_meter: Int32LE
y_pixels_per_meter: Int32LE

colors_used: UInt32LE
colors_important: UInt32LE

# Pixel Data
var bytes_per_pixel = 3  # 24-bit = 3 bytes
var row_size_unpadded = width * bytes_per_pixel
var row_size_padded = align_up(row_size_unpadded, 4)  # Rows aligned to 4 bytes
var total_pixel_data = row_size_padded * height

pixel_data: Bytes(total_pixel_data)
```

**What gets rejected (data errors):**

- Non-BMP files: `expect magic == "BM"`
- Extended or non-standard headers: `expect header_size == 40`
- Non-positive dimensions: `expect width > 0`, `expect height > 0`
- Top-down images (negative height): `expect height > 0`
- Non-RGB formats (8-bit indexed, 16-bit, 32-bit): `expect bits_per_pixel == 24`
- Compressed formats (RLE, etc.): `expect compression == 0`
- Invalid planes: `expect planes == 1`
- Extra data after pixel data (unconsumed data causes error)

---

## Example 3: WAVE Audio (Extended Format Support)

**Format Restrictions:**

This example describes a much broader subset of WAVE than Example 1, covering ~90% of common WAVE files:

**Supported:**

- **PCM (format 1)**: 8-bit, 16-bit, 24-bit, 32-bit
- **IEEE_FLOAT (format 3)**: 32-bit, 64-bit
- **EXTENSIBLE (format 0xFFFE)**: PCM and IEEE_FLOAT subtypes only (via GUID)
- **Channels**: 1 to 8 channels
- **Sample rates**: 8 kHz to 384 kHz
- **Extended fmt chunks** (for IEEE_FLOAT and EXTENSIBLE formats)
- **Even-byte chunk padding** (per RIFF specification)

**Not Supported (will be rejected):**

- RF64 or RIFX (big-endian) containers
- Compressed formats (ADPCM, MP3, μ-law, A-law, etc.)
- EXTENSIBLE with non-PCM/non-FLOAT subtypes
- More than 8 channels
- Sample rates outside 8-384 kHz range
- Additional chunks (fact, LIST, INFO, cue, etc.)
- Multiple fmt or data chunks
- Chunks in wrong order (data must immediately follow fmt)
- ValidBitsPerSample ≠ BitsPerSample in EXTENSIBLE format

**What This Example Teaches (beyond Examples 1 & 2):**

- **`enum` definitions** for named constants (`WaveFormat`)
- **`in` operator** for enum membership testing (`expect core.AudioFormat in WaveFormat`)
- **`when` with braces** for grouping multiple statements in conditionals
- **`pad_to`** for size-bounded records (enforcing chunk sizes)
- **`pad_align`** for chunk alignment (even-byte RIFF boundaries)
- **Nested unions** (unions within unions for sample types)
- **Nested switch expressions** (EXTENSIBLE format resolution)
- **GUID validation** (comparing 16-byte arrays)
- **`size()` function** for validating container sizes
- **Complex multi-level validation** with interdependent fields
- **Record decomposition** for managing complexity

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
  when core.AudioFormat == WaveFormat.PCM        then expect (total_size == 16),
  when core.AudioFormat == WaveFormat.IEEE_FLOAT then expect (total_size >= 18),
  when core.AudioFormat == WaveFormat.EXTENSIBLE then expect (total_size >= 40),

  when total_size > 16 then extra: FmtExtra(total_size),

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

## Next Steps

- **[Reference](reference.md)** - Complete language reference
- Review actual format specifications for formats you need to describe
- Test descriptions against real files
- Iterate based on what works
