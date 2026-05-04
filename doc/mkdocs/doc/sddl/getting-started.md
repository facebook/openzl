# Getting Started with SDDL

This guide walks you through writing your first SDDL description, building up from a minimal example to a complete real-world format.

## What is SDDL?

SDDL (Simple Data Description Language) lets you describe binary file formats so that OpenZL can efficiently decompose them into typed streams for compression. You write a description of your format, the SDDL compiler translates it to bytecode, and the SDDL engine uses that bytecode to parse and split your data.

Instead of treating a file as an opaque blob of bytes, SDDL lets you tell the compressor "these 8 bytes are a double-precision float, these 4 bytes are an unsigned integer, ..." — so it can group similar data together and compress it more effectively.

## Your First Description

The simplest useful description is a single field consumption:

```sddl
: Byte[]
```

This consumes the entire input as an array of bytes. Let's break this down:

- `:` is the **consumption operator** — it reads bytes from the input and associates them with a type
- `Byte` is a single-byte type
- `[]` makes it an **auto-sized array** — it repeats until all input is consumed

This doesn't do anything useful for compression (it's equivalent to treating the file as raw bytes), but it shows the basic mechanics.

## Adding Structure

We'll use a real format for the rest of this guide: the [SAO Star Catalog](http://tdc-www.harvard.edu/software/catalogs/catalogsb.html), which stores astronomical data as a flat array of fixed-size records.

Each star entry is 28 bytes containing coordinates, spectral type, magnitude, and proper motion:

```sddl
record StarEntry() {
  SRA0:  Float64LE,     # Right Ascension (radians, 8 bytes)
  SDEC0: Float64LE,     # Declination (radians, 8 bytes)
  ISP:   Bytes(2),      # Spectral type (2 bytes)
  MAG:   Int16LE,       # Magnitude (2 bytes)
  XRPM:  Float32LE,     # R.A. proper motion (4 bytes)
  XDPM:  Float32LE      # Dec. proper motion (4 bytes)
}
```

Key points:

- **Records** are declared with `record Name() { ... }` and group related fields
- Fields use `name: Type` syntax, separated by commas
- **Endianness is always explicit** — `Float64LE` means 64-bit little-endian float, `Int16LE` means 16-bit little-endian signed integer
- `Bytes(n)` consumes exactly `n` bytes as raw (untyped) data
- Comments start with `#`

## Describing the File

The SAO file has a 28-byte header followed by star entries. For now, let's skip the header and just describe the repeating entries:

```sddl
record StarEntry() {
  SRA0:  Float64LE,
  SDEC0: Float64LE,
  ISP:   Bytes(2),
  MAG:   Int16LE,
  XRPM:  Float32LE,
  XDPM:  Float32LE
}

header: Byte[28]
stars: StarEntry[]
```

- `header: Byte[28]` consumes 28 bytes and stores the result in a variable called `header`
- `stars: StarEntry[]` consumes the remaining input as an auto-sized array of `StarEntry` records — the engine calculates how many entries fit in the remaining bytes

This is already a working description — you could use it to compress the SAO file right now. But we can do better by parsing the header properly.

## Parsing the Header

Instead of treating the header as raw bytes, let's define its structure:

```sddl
record CatalogHeader() {
  STAR0: Int32LE,   # Subtract from star number to get sequence number
  STAR1: Int32LE,   # First star number in file
  STARN: Int32LE,   # Number of stars in the file
  STNUM: Int32LE,   # Star numbering scheme
  MPROP: Int32LE,   # Proper motion info: 0=none, 1=included, 2=with velocity
  NMAG:  Int32LE,   # Number of magnitude values per star
  NBENT: Int32LE    # Bytes per star entry
}

record StarEntry() {
  SRA0:  Float64LE,
  SDEC0: Float64LE,
  ISP:   Bytes(2),
  MAG:   Int16LE,
  XRPM:  Float32LE,
  XDPM:  Float32LE
}

header: CatalogHeader
stars: StarEntry[]
```

Now `header` is a structured variable. We can access its fields with **dot notation** — for example, `header.STARN` gives us the number of stars.

## Adding Validation

The `expect` statement lets you validate format constraints at parse time. If a condition is false, parsing fails with an error — catching corrupt or misidentified files early:

```sddl
header: CatalogHeader

# Verify the entry size matches what we expect
expect header.NBENT == sizeof(StarEntry)

stars: StarEntry[]
```

- `sizeof` returns the size in bytes of a type (only works on types with statically known sizes)
- If the header says entries are a different size than our `StarEntry` definition, something is wrong — `expect` catches this immediately

## Making It Flexible

The simplified description above assumes every star entry has the same fixed layout. But the full SAO format is more complex: depending on header flags, entries can include optional fields like catalog numbers, magnitude arrays, proper motion, radial velocity, and object names.

SDDL handles this with **parameterized records** and **conditional fields**:

```sddl
record CatalogHeader() {
  STAR0: Int32LE,   # Subtract from star number to get sequence number
  STAR1: Int32LE,   # First star number in file
  STARN: Int32LE,   # Number of stars; <0 → coordinates J2000
  STNUM: Int32LE,   # ID scheme / name flag
  MPROP: Int32LE,   # Motion info: 0=none, 1=proper, 2=radial
  NMAG:  Int32LE,   # Number of magnitudes (0–10)
  NBENT: Int32LE    # Bytes per star entry
}

record StarEntry(STNUM, MPROP, NMAG) {
  when STNUM > 0 { XNO: Float32LE },               # Catalog number
  SRA0:  Float64LE,                                # Right Ascension
  SDEC0: Float64LE,                                # Declination
  ISP:   Bytes(2),                                 # Spectral type
  when abs(NMAG) > 0 { MAG: Int16LE[abs(NMAG)] },  # Magnitudes
  when MPROP >= 1 {
    XRPM: Float32LE,                               # R.A. proper motion
    XDPM: Float32LE                                # Dec. proper motion
  },
  when MPROP == 2 { SVEL: Float64LE },             # Radial velocity
  when STNUM < 0 { NAME: Bytes(-STNUM) }           # Object name
}

# File structure
header: CatalogHeader

# Parse the header to get the number of stars and entry parameters
STNUM = header.STNUM
MPROP = header.MPROP
NMAG  = header.NMAG
NBENT = header.NBENT
record_count = abs(header.STARN)

expect sizeof(StarEntry(STNUM, MPROP, NMAG)) == NBENT

stars: StarEntry(STNUM, MPROP, NMAG)[record_count]
```

This description handles the full SAO format — both B1950 and J2000 coordinate systems, variable magnitude counts, optional motion data, and optional object names. Here's what's new:

- **Parameterized records**: `Record StarEntry(STNUM, MPROP, NMAG)` accepts parameters that control which fields are included
- **`when` blocks**: `when STNUM >= 0 { ... }` conditionally includes fields based on a runtime value
- **Variable assignment**: `record_count = abs(header.STARN)` computes a value from an expression
- **`abs()`**: Built-in function returning the absolute value of an integer
- **`sizeof` with parameters**: `sizeof(StarEntry(...))` computes the size of a parameterized record with specific arguments
- **Computed array length**: `StarEntry(...)[record_count]` uses a variable as the array size

## Running Your Description

### Using the CLI Profile

```sh
./zli compress --profile sddl2 --profile-arg desc.sddl --train-inline my_input -o my_input.zl
```

### Training Once, Compressing Many

```sh
./zli train --profile sddl2 --profile-arg desc.sddl input_dir/ -o trained.zlc

for f in $(ls input_dir/); do
  ./zli compress --compressor trained.zlc input_dir/$f -o output_dir/$f.zl
done
```

## Syntax Highlighting

SDDL files use the `.sddl` extension. For the best editing experience, syntax highlighting is available for VS Code.

### VS Code

The OpenZL repository includes a VS Code extension for SDDL syntax highlighting. See `contrib/sddl-syntax-highlighting/README.md` for installation instructions.

## Next Steps

- [Core Concepts](core-concepts.md) — detailed explanation of types, records, arrays, variables, and expressions
- [Conditional Fields](conditional-fields.md) — `when` blocks in depth
- [Validation](validation.md) — `expect` statements
- [Examples](examples.md) — more complete format descriptions
- [Quick Reference](reference.md) — all supported syntax at a glance
