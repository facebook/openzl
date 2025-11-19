# Conditional & Variant Data

*Chapter 7 - Unions and conditional fields*

Binary formats often contain optional fields, variant data, or structures that vary by version or type. This chapter covers SDDL's constructs for describing these patterns: conditional fields with `when` and variant data with `Union`.

For a comprehensive overview of SDDL's core language elements including conditional constructs and unions, see [Language Elements Overview](core-concepts.md#language-elements-overview).

Refer to the [coverage map entries for conditional fields](real-formats.md#coverage-when-then) and [variant unions](real-formats.md#coverage-unions) whenever you need a full specification that uses these features.

---

## Conditional Fields with `when`

The `when` keyword makes fields appear only when a condition is true.

### Basic Syntax

```sddl
Record Packet(include_timestamp) = {
  id: Int32LE,
  size: Int16LE,
  when include_timestamp { timestamp: Int64LE }
}
```

If `include_timestamp` is true, the record includes a timestamp. If false, it doesn't.

### Parameter-Based Conditions

Conditions based on parameters maintain instant-parse status:

```sddl
Record Data(has_checksum, version) = {
  payload: Bytes(100),
  when has_checksum { crc: UInt32LE },
  when version >= 2 { metadata: Bytes(64) }
}
```

The layout is fully determined by the parameters. This is instant-parse.

### Field-Based Conditions

Conditions referencing parsed fields require scanning:

```sddl
Record Header() = {
  flags: UInt8,
  when (flags & 0x01) != 0 { checksum: UInt32LE }  # Requires scan
}
```

The parser must read `flags` before knowing whether `checksum` exists.

### Multiple Conditions

```sddl
Record VersionedData(version) = {
  base: Int32LE,
  when version >= 1 { feature_v1: Int32LE },
  when version >= 2 { feature_v2: Int64LE },
  when version >= 3 { feature_v3: ExtendedData }
}
```

Each condition is evaluated independently. Multiple can be true simultaneously.

### Complex Conditions

Use logical operators for more sophisticated conditions:

```sddl
Record Data(mode, flags) = {
  base: Int32LE,
  when mode == 1 or mode == 3 { option_a: Int32LE },
  when (flags & 0x80) != 0 and mode >= 2 { option_b: Int64LE }
}
```

### Block Form with Braces

When you need multiple fields or statements under a condition, use braces instead of `then`:

```sddl
Record ExtendedData(has_extension) = {
  base: BaseData,
  when has_extension {
    ext_field1: Int32LE,
    ext_field2: Int64LE,
    var ext_size = ext_field1 + ext_field2,
    expect ext_field1 > 0
  }
}
```

The block can contain fields, variables, and `expect` statements. When using braces, omit the `then` keyword.

---

## Unions for Variant Data

Unions represent "exactly one of several options" based on a selector value.

### Basic Union Structure

```sddl
Union Payload(type_code) = {
  case 1: ImageData,
  case 2: AudioData,
  case 3: VideoData,
  default: RawBytes
}
```

**Key points:**
- First parameter is the dispatch selector
- Only one case is active based on the selector value
- `default` handles unmatched values

### Using Unions

```sddl
Record Frame() = {
  type: UInt8,
  size: UInt32LE,
  payload: Union(type) {
    case 1: Image(size),
    case 2: Audio(size),
    case 3: Video(size),
    default: Bytes(size)
  }
}
```

The `type` field determines which case is parsed.

### Multiple Cases for Same Type

Multiple predicate values can map to the same type.

```sddl
Union Protocol(version) = {
  case 1, 2, 3: LegacyFormat(version),    # Versions 1-3
  case 4:       ModernFormat(version),    # Version  4
  case 5, 6:    EnhancedFormat(version),  # Versions 5-6
  default:      UnknownFormat
}
```

### Range-Based Cases

Use ranges for contiguous value sets:

```sddl
Union DataBlock(block_type) = {
  case 0x00..0x0F: SmallBlock,     # Types 0-15
  case 0x10..0x1F: MediumBlock,    # Types 16-31
  case 0x20..0x7F: LargeBlock,     # Types 32-127
  default: CustomBlock
}
```

Ranges are inclusive on both ends.

### The `default` Case

The `default` case handles selector values that don't match any explicit case:

```sddl
Union Message(msg_type) = {
  case 1: TextMessage,
  case 2: ImageMessage,
  case 3: AudioMessage,
  default: UnknownMessage  # Handles any other value
}
```

**Without `default`:** If the selector value doesn't match any case, parsing fails with a data error. Always include `default` unless you're certain all possible values are covered by explicit cases.

```sddl
Union StrictMessage(msg_type) = {
  case 1: TextMessage,
  case 2: ImageMessage,
  case 3: AudioMessage
  # No default: msg_type=4 causes data error!
}
```

### Overlapping Ranges

Overlapping ranges are a format error:

!!! danger "Format error example"
    ```sddl
    Union Bad(type) = {
      case 1..10: TypeA,
      case 5..15: TypeB,   # ERROR: 5-10 overlap with first case!
    }
    ```

The compiler rejects this at compile time.

---

## Unions and Instant-Parse

A union is instant-parse only if:
1. The selector is a parameter or constant (not a parsed field)
2. All case arms are themselves instant-parse

### Instant-Parse Union

```sddl
Record Packet(type, size) = {
  payload: Union(type) {    # type is parameter: instant-parse
    case 1: Image(size),    # size is parameter: instant-parse
    case 2: Audio(size),
    default: Raw(size)
  }
} @instant_parse
```

### Non-Instant-Parse Union

```sddl
Record Packet() = {
  type: UInt8,            # Parsed field
  size: UInt32LE,
  payload: Union(type) {  # Depends on parsed 'type': requires scan
    case 1: Image(size),
    case 2: Audio(size),
    default: Raw(size)
  }
}
```

---

## Enumerations

Enums provide named constants for discriminators and flags, to improve readability.
For all intents and purposes, they are treated as constants.

Enum values must always be set explicitly.

Enums currently only support integer values.
Plans to also support string constants in the future are being considered.

### Defining Enums

```sddl
enum MessageType {
  TEXT = 1,
  IMAGE = 2,
  AUDIO = 3,
  VIDEO = 4
}

enum Flags {
  READ   = 1 << 0,   # 0x01
  WRITE  = 1 << 1,   # 0x02
  EXEC   = 1 << 2    # 0x04
}
```

### Using Enums in Unions

```sddl
Record Message() = {
  type: UInt8,
  payload: Union(type) {
    case MessageType.TEXT: TextPayload,
    case MessageType.IMAGE: ImagePayload,
    case MessageType.AUDIO: AudioPayload,
    case MessageType.VIDEO: VideoPayload,
    default: UnknownPayload
  }
}
```

### Using Enums in Conditions

```sddl
enum FileFlags {
  HAS_METADATA = 0x01,
  HAS_CHECKSUM = 0x02,
  COMPRESSED   = 0x04
}

Record FileData(flags) = {
  data: Bytes(100),
  when (flags & FileFlags.HAS_METADATA) != 0 { metadata: Metadata },
  when (flags & FileFlags.HAS_CHECKSUM) != 0 { checksum: UInt32LE }
}
```

### Enum Membership Testing with `in`

Test if a value belongs to an enum set:

```sddl
enum WaveFormat {
  PCM = 1,
  IEEE_FLOAT = 3,
  EXTENSIBLE = 0xFFFE
}

Record Audio() = {
  format: UInt16LE,
  expect format in WaveFormat,  # Requires format to be 1, 3, or 0xFFFE

  data: Bytes(1024)
}
```

The `in` operator checks if a value matches any of the enum's defined constants. It's typically used within `expect` statements for validation and causes a data error if the value doesn't match.

---

## Inline Declarations

### Inline Records

Define anonymous record types directly where used:

```sddl
Record Packet() = {
  header: Record() {
    magic: Bytes(4),
    version: UInt16LE,
    flags: UInt16LE
  },
  data: Bytes(256)
}
```

Use inline records when the type is used only once and is simple.

### Inline Unions

```sddl
Record Frame(type, size) = {
  id: UInt32LE,
  payload: Union(type, size) {
    case 1: Image(size),
    case 2: Audio(size),
    case 3: Video(size)
  }
}
```

### Named vs Inline

**Named Types:**
```sddl
Record Header() = { magic: Bytes(4), version: UInt16LE }
Record Packet() = { header: Header, data: Bytes(256) }
```

- Reusable across multiple records
- Self-documenting (type has a name)
- Can be tested independently

**Inline Types:**
```sddl
Record Packet() = {
  header: Record() { magic: Bytes(4), version: UInt16LE },
  data: Bytes(256)
}
```

- Keeps definition close to usage
- Less namespace pollution
- Not reusable

---

## Common Patterns

### Pattern: Tagged Union

```sddl
enum VariantType { INT = 1, FLOAT = 2, STRING = 3 }

Record Variant() = {
  tag: UInt8,
  value: Union(tag) {
    case VariantType.INT: Int64LE,
    case VariantType.FLOAT: Float64LE,
    case VariantType.STRING: Record() {
      length: UInt32LE,
      text: Bytes(length)
    }
  }
}
```

Note that when you using **anonymous `Record() { ... }`** in a Union case (without a field name), the fields of that Record become **direct members** of the Union result.

### Pattern: Version-Specific Fields

```sddl
Record FileFormat() = {
  magic: Bytes(4),
  version: UInt16LE,
  data: Bytes(100),

Record FileWithMetadata(version) = {
  header: FileHeader,
  when version >= 2 { extended_header: ExtendedHeader },

  data: Bytes(1024),
  when version >= 3 { metadata: Metadata },
  when version >= 3 { checksum: UInt32LE }
}
```

### Pattern: Optional Extensions

```sddl
enum Extensions {
  COMPRESSION = 0x01,
  ENCRYPTION  = 0x02,
  METADATA    = 0x04
}

Record Document() = {
  flags: UInt8,
  core_data: Bytes(512),

  when (flags & Extensions.COMPRESSION) != 0
    then compression_info: CompressionHeader,

  when (flags & Extensions.ENCRYPTION) != 0
    then encryption_info: EncryptionHeader,

  when (flags & Extensions.METADATA) != 0
    then metadata: Metadata
}
```

### Pattern: Discriminated Payload

```sddl
enum CommandType {
  REQUEST  = 1,
  RESPONSE = 2,
  ERROR    = 3
}

Record NetworkMessage() = {
  message_id: UInt32LE,
  command_type: UInt8,
  payload_length: UInt32LE,

  payload: Union(command_type) {
    case CommandType.REQUEST: Request(payload_length),
    case CommandType.RESPONSE: Response(payload_length),
    case CommandType.ERROR: Error(payload_length)
  }
}
```

---

## Practical Examples

### Example 1: PNG-Like Chunk Structure

```sddl
enum ChunkType {
  HEADER  = 0x49484452,  # "IHDR"
  PALETTE = 0x504C5445,  # "PLTE"
  DATA    = 0x49444154,  # "IDAT"
  END     = 0x49454E44   # "IEND"
}

Record Chunk() = {
  length: UInt32BE,
  type: UInt32BE,

  data: Union(type) {
    case ChunkType.HEADER: ImageHeader,
    case ChunkType.PALETTE: Palette,
    case ChunkType.DATA: ImageData(length),
    case ChunkType.END: Bytes(0),
    default: Bytes(length)  # Unknown chunk types preserved
  },

  crc: UInt32BE
}
```

### Example 2: Extensible Configuration Format

```sddl
enum ConfigVersion { V1 = 1, V2 = 2, V3 = 3 }

Record Config() = {
  version: UInt16LE,

  # V1 fields
  name_length: UInt16LE,
  name: Bytes(name_length),
Record Config(version) = {
  host: Bytes(256),
  port: UInt16LE,
  when version >= ConfigVersion.V2 { timeout: UInt32LE },
  when version >= ConfigVersion.V2 { max_connections: UInt16LE },

  # V3 additions
  when version >= ConfigVersion.V3 { tls_config: TLSConfig },
  when version >= ConfigVersion.V3 { flags: UInt32LE }
}
```

### Example 3: Format Migration

```sddl
Record MultiVersionFile() = {
  magic: Bytes(4),
  version: UInt16LE,

  body: Union(version) {
    case 1: V1Format,
    case 2: V2Format,
    case 3: V3Format,
    # if version is any other value, it's an error
  }
}
```

---

## Common Pitfalls

### Pitfall: Assuming Instant-Parse with Field Selectors

!!! error "Instant-parse error example"
    ```sddl
    Record Bad() = {
      type: UInt8,
      payload: Union(type) {  # 'type' is parsed field
        case 1: Data1,
        case 2: Data2
      } @instant_parse  # This one is okay, assuming Data1 and Data2 are instant-parse
    } @instant_parse  # Compilation error: this Record is not instant-parse
    ```

---

## Summary

Use `when` to gate fields or blocks on conditions: parameter-based tests keep the construct instant-parse, while tests against local fields introduce scan requirements. Use `Union(selector)` when exactly one of several layouts is valid, supplying parameter-driven selectors whenever possible and a `default` case for robustness. Enums keep selectors and conditions readable and pair naturally with unions or `when`. Overlapping union ranges are format errors, so plan selector ranges carefully.

---

## Where to Go Next

- **[Variables and Expressions](variables-expressions.md)** to compute selectors and condition flags.
- **[Best Practices](best-practices.md)** for guidance on evolving formats that use conditionals.
- **[Real-World Formats](real-formats.md)** to see unions and `when` blocks in context.
