# Real-World Formats

*Chapter 10 - Complete examples*

This chapter presents complete SDDL descriptions for real-world formats, demonstrating how the language features work together in practice.

---

## Example 1: BMP Image Format

BMP is a simple raster image format with a fixed header followed by pixel data.

```sddl
# BMP File Format
Record BMPFileHeader() = {
  magic: Bytes(2),
  expect magic == "BM",

  file_size: UInt32LE,
  reserved1: UInt16LE,
  reserved2: UInt16LE,
  pixel_data_offset: UInt32LE
}

Record BMPInfoHeader() = {
  header_size: UInt32LE,
  expect header_size == 40,  # Standard BITMAPINFOHEADER

  width: Int32LE,
  height: Int32LE,
  planes: UInt16LE,
  expect planes == 1,

  bits_per_pixel: UInt16LE,
  expect bits_per_pixel == 24 or bits_per_pixel == 32,

  compression: UInt32LE,
  expect compression == 0,  # Uncompressed only

  image_size: UInt32LE,
  x_pixels_per_meter: Int32LE,
  y_pixels_per_meter: Int32LE,
  colors_used: UInt32LE,
  colors_important: UInt32LE
}

Record BMPFile() = {
  file_header: BMPFileHeader,
  info_header: BMPInfoHeader,

  var bytes_per_pixel = info_header.bits_per_pixel / 8,
  var row_size_unpadded = info_header.width * bytes_per_pixel,
  var row_size_padded = align_up(row_size_unpadded, 4),
  var total_pixel_data = row_size_padded * abs(info_header.height),

  pixel_data: Bytes(total_pixel_data)
}

bmp: BMPFile
```

**Key points:**
- Fixed-size headers are instant-parse
- Pixel data size computed from dimensions
- Row alignment to 4-byte boundaries

---

## Example 2: WAVE Audio Format

WAVE uses a chunk-based structure (RIFF container).

```sddl
# WAVE Audio Format
Record RIFFChunkHeader() = {
  chunk_id: Bytes(4),
  chunk_size: UInt32LE
}

Record WAVEFormatChunk() = {
  audio_format: UInt16LE,
  expect audio_format == 1,  # PCM

  num_channels: UInt16LE,
  expect num_channels >= 1 and num_channels <= 2,

  sample_rate: UInt32LE,
  byte_rate: UInt32LE,
  block_align: UInt16LE,
  bits_per_sample: UInt16LE,
  expect bits_per_sample == 8 or bits_per_sample == 16
}

Record WAVEChunk() = {
  header: RIFFChunkHeader,

  data: Union(header.chunk_id) {
    case "fmt ": WAVEFormatChunk,
    case "data": Bytes(header.chunk_size),
    default: Bytes(header.chunk_size)  # Skip unknown chunks
  }
}

Record WAVEFile() = {
  riff_header: RIFFChunkHeader,
  expect riff_header.chunk_id == "RIFF",

  wave_id: Bytes(4),
  expect wave_id == "WAVE",

  chunks: scan WAVEChunk[]
}

wave: WAVEFile
```

**Key points:**
- Chunk-based structure with union for variant chunks
- `scan` keyword for sequential chunk parsing
- `default` case handles unknown chunk types

---

## Example 3: TAR Archive

TAR files consist of 512-byte blocks containing headers and file data.

```sddl
# TAR Archive Format (POSIX ustar)
Record TARHeader() = {
  name: Bytes(100),
  mode: Bytes(8),
  uid: Bytes(8),
  gid: Bytes(8),
  size: Bytes(12),          # Octal ASCII string
  mtime: Bytes(12),         # Octal ASCII string
  checksum: Bytes(8),
  typeflag: Bytes(1),
  linkname: Bytes(100),
  magic: Bytes(6),
  version: Bytes(2),
  uname: Bytes(32),
  gname: Bytes(32),
  devmajor: Bytes(8),
  devminor: Bytes(8),
  prefix: Bytes(155),
  _: Bytes(12)              # Padding to 512 bytes
} pad_to 512

Record TAREntry() = {
  header: TARHeader,

  # Parse size from octal ASCII
  var file_size = 0,  # Would need octal parsing function

  var num_blocks = ceil_div(file_size, 512),
  var padded_size = num_blocks * 512,

  data: Bytes(padded_size)
}

# TAR files are sequences of entries until two zero blocks
entries: scan TAREntry[]
```

**Key points:**
- Fixed 512-byte blocks with `pad_to`
- ASCII-encoded numbers (would need parsing in real implementation)
- Variable-length entries with block alignment

---

## Example 4: PNG Chunk Structure

PNG uses a clean chunk-based design with CRC validation.

```sddl
# PNG File Format
enum PNGChunkType {
  IHDR = 0x49484452,
  PLTE = 0x504C5445,
  IDAT = 0x49444154,
  IEND = 0x49454E44
}

Record PNGIHDRData() = {
  width: UInt32BE,
  height: UInt32BE,
  bit_depth: UInt8,
  color_type: UInt8,
  compression: UInt8,
  filter: UInt8,
  interlace: UInt8
}

Record PNGChunk() = {
  length: UInt32BE,
  type: UInt32BE,

  data: Union(type) {
    case PNGChunkType.IHDR: PNGIHDRData,
    case PNGChunkType.IDAT: Bytes(length),
    case PNGChunkType.IEND: Bytes(0),
    default: Bytes(length)
  },

  crc: UInt32BE
}

Record PNGFile() = {
  signature: Bytes(8),
  expect signature == "\x89PNG\r\n\x1a\n",

  chunks: scan PNGChunk[]
}

png: PNGFile
```

**Key points:**
- Big-endian integers (network byte order)
- Enum for chunk types
- Union dispatches on chunk type
- CRC field present but not validated in description

---

## Example 5: Simple Custom Protocol

A hypothetical network protocol demonstrating common patterns.

```sddl
# Custom Binary Protocol
enum MessageType {
  HEARTBEAT = 0x01,
  REQUEST   = 0x02,
  RESPONSE  = 0x03,
  ERROR     = 0x04
}

Record MessageHeader() = {
  magic: UInt16BE,
  expect magic == 0x4D53,  # "MS"

  version: UInt8,
  message_type: UInt8,
  sequence_id: UInt32BE,
  payload_length: UInt32BE,
  flags: UInt16BE
}

Record RequestPayload(length) = {
  method: UInt8,
  param_count: UInt8,
  params: Bytes(length - 2)
}

Record ResponsePayload(length) = {
  status_code: UInt16BE,
  data: Bytes(length - 2)
}

Record ErrorPayload(length) = {
  error_code: UInt16BE,
  message_length: UInt16BE,
  message: Bytes(message_length)
}

Record Message() = {
  header: MessageHeader,

  payload: Union(header.message_type) {
    case MessageType.HEARTBEAT: Bytes(0),
    case MessageType.REQUEST: RequestPayload(header.payload_length),
    case MessageType.RESPONSE: ResponsePayload(header.payload_length),
    case MessageType.ERROR: ErrorPayload(header.payload_length),
    default: Bytes(header.payload_length)
  },

  var has_checksum = (header.flags & 0x0001) != 0,
  when has_checksum then checksum: UInt32BE
}

message: Message
```

**Key points:**
- Version and type fields for extensibility
- Union for different message types
- Conditional checksum based on flags
- Big-endian for network protocol

---

## Example 6: Configuration File Format

A structured configuration format with versioning.

```sddl
# Configuration File Format
enum ConfigVersion { V1 = 1, V2 = 2, V3 = 3 }

Record StringField() = {
  length: UInt16LE,
  text: Bytes(length)
}

Record ConfigV1() = {
  app_name: StringField,
  port: UInt16LE,
  timeout_seconds: UInt32LE
}

Record ConfigV2() = {
  # V1 fields
  app_name: StringField,
  port: UInt16LE,
  timeout_seconds: UInt32LE,

  # V2 additions
  max_connections: UInt16LE,
  log_level: UInt8
}

Record ConfigV3() = {
  # V1 fields
  app_name: StringField,
  port: UInt16LE,
  timeout_seconds: UInt32LE,

  # V2 fields
  max_connections: UInt16LE,
  log_level: UInt8,

  # V3 additions
  enable_tls: UInt8,
  cert_path: StringField,
  key_path: StringField
}

Record ConfigFile() = {
  magic: Bytes(4),
  expect magic == "CONF",

  version: UInt16LE,

  config: Union(version) {
    case ConfigVersion.V1: ConfigV1,
    case ConfigVersion.V2: ConfigV2,
    case ConfigVersion.V3: ConfigV3,
    default: Record {
      expect false @err_msg "Unsupported config version"
    }
  }
}

config: ConfigFile
```

**Key points:**
- Explicit version handling
- Union for version-specific structures
- Progressive field addition across versions
- Helper record for length-prefixed strings

---

## Common Patterns Observed

### Magic Numbers

Most formats start with a signature:

```sddl
magic: Bytes(4),
expect magic == "RIFF"
```

### Length-Prefixed Data

Common pattern for variable-length fields:

```sddl
length: UInt32LE,
data: Bytes(length)
```

### Chunk/Block Structure

Many formats use repeated chunks:

```sddl
Record Chunk() = {
  header: ChunkHeader,
  data: Bytes(header.size)
}

chunks: scan Chunk[]
```

### Version-Based Evolution

Handle multiple format versions:

```sddl
version: UInt16LE,
when version >= 2 then extended_data: ExtendedData
```

### Flags for Optional Features

Use bitfields for optional components:

```sddl
flags: UInt8,
when (flags & 0x01) != 0 then optional_field: Data
```

---

## Summary

These examples demonstrate:

- **Structure composition** - Building complex formats from simple records
- **Chunk-based designs** - Using `scan` for sequential structures
- **Versioning** - Unions and conditionals for format evolution
- **Validation** - `expect` for format correctness
- **Endianness** - Explicit LE/BE for portability

Real formats often mix these patterns. Start with the simplest valid case, then add complexity as needed.

---

## Next Steps

- **[Reference](reference.md)** - Complete language reference
- Review actual format specifications for formats you need to describe
- Test descriptions against real files
- Iterate based on what works
