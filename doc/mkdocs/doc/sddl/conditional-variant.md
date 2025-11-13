# Conditional & Variant Data

*Chapter 7 - Handling optional fields, variants, and dynamic structures*

Real-world binary formats often need to represent optional data, variants, or structures that change based on version, flags, or type codes. SDDL provides powerful constructs for describing these scenarios clearly and efficiently.

---

## Conditional Fields with `when`

### Basic Syntax

The `when` keyword allows fields to appear conditionally:

```sddl
Record Message(include_timestamp) = {
  id: Int32LE,
  body: Bytes(256),
  when include_timestamp then timestamp: Int64LE
}
Parameter-Based Conditions (Instant-Parse Safe)
Conditions based on parameters maintain instant-parse status:

Record Packet(version, has_checksum) = {
  header: Header,
  data: Bytes(100),
  when has_checksum then crc: Int32LE,
  when version >= 2 then metadata: Metadata
}
Why it's instant-parse: The layout can be determined from parameters alone, without examining the data.

Field-Based Conditions (Requires Scanning)
Conditions referencing parsed fields break instant-parse:

Record DynamicPacket() = {
  flags: Int16LE,
  data: Bytes(100),
  when (flags & 0x01) then checksum: Int32LE  # Depends on parsed field
}
Why it requires scanning: The parser must read flags before it can determine if checksum exists.

Multiple Conditions
You can have multiple conditional fields:

Record Feature(version) = {
  base: BaseData,
  when version >= 1 then feature_v1: Int32LE,
  when version >= 2 then feature_v2: Int64LE,
  when version >= 3 then feature_v3: ExtendedData
}
Complex Conditions
Use logical operators for sophisticated conditions:

Record Config(mode, flags) = {
  basic: BasicConfig,
  when mode == 1 or mode == 3 then option_a: Int32LE,
  when (flags & 0x80) and mode >= 2 then option_b: Int64LE,
  when mode == 0 then minimal: Bytes(8)
}
Unions for Variant Data
Basic Union Structure
Unions represent "one-of-many" choices, selected by a discriminator:

Union Payload(type_code) = {
  case 1: ImageData,
  case 2: AudioData,
  case 3: VideoData,
  default: RawBytes
}
Key Points:

First parameter is the dispatch selector
Only one case is parsed based on the selector value
default handles unmatched cases
Using the Union
Record Frame() = {
  type: Int8,
  size: Int32LE,
  payload: Union(type) {
    case 1: Image(size),
    case 2: Audio(size),
    case 3: Video(size)
  }
}
Multiple Cases for Same Type
Union Protocol(version) = {
  case 1, 2, 3: LegacyFormat,    # Versions 1-3 use legacy
  case 4: ModernFormat,           # Version 4 introduced new format
  case 5, 6: EnhancedFormat,      # Versions 5-6 enhanced
  default: UnknownFormat
}
Range-Based Cases
Use ranges for continuous value sets:

Union DataBlock(block_type) = {
  case 0x00..0x0F: SmallBlock,     # Types 0-15
  case 0x10..0x1F: MediumBlock,    # Types 16-31
  case 0x20..0x7F: LargeBlock,     # Types 32-127
  case 0x80..0xFF: CustomBlock,    # Types 128-255
}
The default Case
Always use default unless you're certain all values are covered:

Union Message(msg_type) = {
  case 1: TextMessage,
  case 2: ImageMessage,
  case 3: AudioMessage,
  default: UnknownMessage  # Gracefully handle unknown types
}
Without default: Encountering an unmatched value results in a data error.

Error: Overlapping Ranges
This will produce a format error:

Union Bad(type) = {
  case 1..10: TypeA,
  case 5..15: TypeB,   # ERROR: 5-10 overlap!
}
Instant-Parse Considerations for Unions
Instant-Parse Union Requirements
A union is instant-parse only if:

The selector is a parameter or constant (not a parsed field)
All case arms are themselves instant-parse
Instant-Parse Example:

@instant_parse
Record Packet(type, size) = {
  payload: Union(type) {    # type is parameter
    case 1: Image(size),    # size is parameter
    case 2: Audio(size),
    default: Raw(size)
  }
}
Non-Instant-Parse Union
If the selector is a parsed field:

Record Packet() = {
  type: Int8,              # Parsed field
  size: Int32LE,
  payload: Union(type) {   # Depends on parsed 'type'
    case 1: Image(size),
    case 2: Audio(size),
    default: Raw(size)
  }
}  # This record requires scanning
Enumerations
Defining Enums
Enums provide named constants for discriminators:

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
Using Enums in Unions
Record Message() = {
  type: Int8,
  payload: Union(type) {
    case MessageType.TEXT: TextPayload,
    case MessageType.IMAGE: ImagePayload,
    case MessageType.AUDIO: AudioPayload,
    case MessageType.VIDEO: VideoPayload,
    default: UnknownPayload
  }
}
Using Enums in Conditions
enum FileFlags {
  HAS_METADATA = 0x01,
  HAS_CHECKSUM = 0x02,
  COMPRESSED   = 0x04
}

Record File() = {
  flags: Int16LE,
  data: Bytes(1024),
  when (flags & FileFlags.HAS_METADATA) then metadata: Metadata,
  when (flags & FileFlags.HAS_CHECKSUM) then checksum: Int32LE
}
Inline Declarations
Inline Records
Define anonymous record types directly where used:

Record Packet() = {
  header: Record {
    magic: Bytes(4),
    version: Int16LE,
    flags: Int16LE
  },
  data: Bytes(256)
}
When to use inline:

Type is used only once
Type is simple and self-explanatory
Improves locality of definition
Inline Unions
Record Frame(type, size) = {
  id: Int32LE,
  payload: Union(type, size) {
    case 1: Image(size),
    case 2: Audio(size),
    case 3: Video(size)
  }
}
Named vs Inline Trade-offs
Named Types:

Record Header() = { magic: Bytes(4), version: Int16LE }
Record Packet() = { header: Header, data: Bytes(256) }
✅ Reusable
✅ Self-documenting (type has a name)
✅ Can be tested independently

Inline Types:

Record Packet() = {
  header: Record { magic: Bytes(4), version: Int16LE },
  data: Bytes(256)
}
✅ Locality of reference
✅ Less namespace pollution
❌ Not reusable

Design Patterns
Pattern: Tagged Union
A classic variant pattern with explicit type tag:

enum VariantType { INT = 1, FLOAT = 2, STRING = 3 }

Record Variant() = {
  tag: Int8,
  value: Union(tag) {
    case VariantType.INT: Int64LE,
    case VariantType.FLOAT: Float64LE,
    case VariantType.STRING: Record {
      length: Int32LE,
      text: Bytes(length)
    }
  }
}
Pattern: Version-Specific Fields
Evolve formats over time:

Record FileFormat() = {
  magic: Bytes(4),
  version: Int16LE,

  # Core fields (all versions)
  data: Bytes(100),

  # Version 2 additions
  when version >= 2 then extended_header: ExtendedHeader,

  # Version 3 additions
  when version >= 3 then metadata: Metadata,
  when version >= 3 then checksum: Int32LE
}
Pattern: Optional Extensions
Use flags to enable optional features:

enum Extensions {
  COMPRESSION = 0x01,
  ENCRYPTION  = 0x02,
  METADATA    = 0x04
}

Record Document() = {
  flags: Int16LE,
  core_data: Bytes(512),

  when (flags & Extensions.COMPRESSION)
    then compression_info: CompressionHeader,

  when (flags & Extensions.ENCRYPTION)
    then encryption_info: EncryptionHeader,

  when (flags & Extensions.METADATA)
    then metadata: Metadata
}
Pattern: Discriminated Payload
Common in network protocols:

enum CommandType {
  REQUEST  = 1,
  RESPONSE = 2,
  ERROR    = 3
}

Record NetworkMessage() = {
  message_id: Int32LE,
  command_type: Int8,
  payload_length: Int32LE,

  payload: Union(command_type) {
    case CommandType.REQUEST: Request(payload_length),
    case CommandType.RESPONSE: Response(payload_length),
    case CommandType.ERROR: Error(payload_length)
  }
}
Pattern: Polymorphic Containers
Arrays of variant data:

Record Item(type, size) = {
  data: Union(type) {
    case 1: TypeA(size),
    case 2: TypeB(size),
    case 3: TypeC(size)
  }
}

Record Container() = {
  count: Int32LE,
  items: scan Record {
    type: Int8,
    size: Int32LE,
    item: Item(type, size)
  }[count]
}
Pattern: Format Migration
Handle multiple format versions in one description:

Record MultiVersionFile() = {
  magic: Bytes(4),
  version: Int16LE,

  body: Union(version) {
    case 1: V1Format,
    case 2: V2Format,
    case 3: V3Format,
    default: Record {
      expect false @err_msg "Unsupported version"
    }
  }
}
Practical Examples
Example 1: PNG-Like Chunk Structure
enum ChunkType {
  HEADER = 0x49484452,  # "IHDR"
  PALETTE = 0x504C5445, # "PLTE"
  DATA = 0x49444154,    # "IDAT"
  END = 0x49454E44      # "IEND"
}

Record Chunk() = {
  length: Int32BE,
  type: Int32BE,

  data: Union(type) {
    case ChunkType.HEADER: ImageHeader,
    case ChunkType.PALETTE: Palette,
    case ChunkType.DATA: ImageData(length),
    case ChunkType.END: Bytes(0),
    default: Bytes(length)  # Unknown chunk types preserved
  },

  crc: Int32BE
}
Example 2: Protocol Buffer Style Message
enum FieldType {
  VARINT = 0,
  FIXED64 = 1,
  LENGTH_DELIMITED = 2,
  FIXED32 = 5
}

Record Field() = {
  tag_and_type: Int8,
  var tag = tag_and_type >> 3,
  var type = tag_and_type & 0x07,

  value: Union(type) {
    case FieldType.VARINT: VarInt,
    case FieldType.FIXED64: Int64LE,
    case FieldType.LENGTH_DELIMITED: Record {
      length: VarInt,
      data: Bytes(length)
    },
    case FieldType.FIXED32: Int32LE
  }
}
Example 3: Extensible Configuration Format
enum ConfigVersion { V1 = 1, V2 = 2, V3 = 3 }

Record Config() = {
  version: Int16LE,

  # V1 fields
  name_length: Int16LE,
  name: Bytes(name_length),
  port: Int16LE,

  # V2 additions
  when version >= ConfigVersion.V2 then timeout: Int32LE,
  when version >= ConfigVersion.V2 then max_connections: Int16LE,

  # V3 additions
  when version >= ConfigVersion.V3 then tls_config: TLSConfig,
  when version >= ConfigVersion.V3 then flags: Int32LE
}
Common Pitfalls
Pitfall 1: Forgetting default
# BAD: No default case
Union Message(type) = {
  case 1: TextMessage,
  case 2: ImageMessage
}  # Data error if type == 3!

# GOOD: Always provide default
Union Message(type) = {
  case 1: TextMessage,
  case 2: ImageMessage,
  default: UnknownMessage
}
Pitfall 2: Overlapping Conditions
# CONFUSING: Both conditions can be true
Record Ambiguous(flags) = {
  when (flags & 0x01) then field_a: Int32LE,
  when (flags & 0x03) then field_b: Int32LE  # Also true when 0x01!
}

# BETTER: Use mutually exclusive conditions
Record Clear(flags) = {
  when (flags & 0x01) and not (flags & 0x02) then field_a: Int32LE,
  when (flags & 0x03) == 0x03 then field_b: Int32LE
}
Pitfall 3: Assuming Instant-Parse with Field Selectors
# This is NOT instant-parse!
@instant_parse  # Compilation error
Record Bad() = {
  type: Int8,
  payload: Union(type) {  # 'type' is parsed field
    case 1: Data1,
    case 2: Data2
  }
}

# Fix: Pass as parameter
@instant_parse  # OK
Record Good(type) = {
  payload: Union(type) {  # 'type' is parameter
    case 1: Data1,
    case 2: Data2
  }
}
Performance Considerations
Instant-Parse Unions Are Free
When a union is instant-parse, the compiler can generate optimal code:

@instant_parse
Record Fast(type, size) = {
  payload: Union(type) {
    case 1: Image(size),   # All layout known upfront
    case 2: Audio(size),
    default: Raw(size)
  }
}
Field-Based Selection Requires Branching
Record Slower() = {
  type: Int8,            # Must read first
  payload: Union(type) { # Then branch
    case 1: Image(100),
    case 2: Audio(100)
  }
}
Impact: Minimal for single unions, but consider batching if processing millions of records.

SoA Limitations with Conditionals
Structure-of-Arrays cannot be used with conditional fields:

# NOT ALLOWED
Record Bad(has_field) = {
  when has_field then optional: Int32LE
}

items: soa Bad(true)[1000]  # Error!
Testing Conditional Logic
Test Matrix
For conditional fields, test:

✅ Condition true
✅ Condition false
✅ Boundary values
✅ Multiple conditions combinations
Test Cases for Unions
✅ Each case arm
✅ Default case
✅ Boundary values (min/max of ranges)
✅ Invalid values (if no default)
Summary
Feature	Use When	Instant-Parse Safe?
when with parameters	Optional fields, version checks	✅ Yes
when with parsed fields	Dynamic conditionals	❌ No
Union with parameter selector	Type-based variants	✅ Yes
Union with field selector	Dynamic type selection	❌ No
Enums	Named constants	✅ Yes (values only)
Inline records/unions	One-off types	Depends on contents
Next Steps
Variables and Expressions - Computing values for conditions
Best Practices - Patterns for format evolution
Language Reference - Complete syntax details
