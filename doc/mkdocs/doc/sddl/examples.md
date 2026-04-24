# Examples

Complete SDDL descriptions for real binary formats.

## SAO Star Catalog (Simplified)

The [Silesia compression corpus](https://sun.aei.polsl.pl/~sdeor/index.php?page=silesia) includes a file from the SAO (Smithsonian Astronomical Observatory) star catalog. This simplified description treats every entry as having the same fixed layout — 28 bytes of coordinates, spectral type, magnitude, and proper motion:

```sddl
record StarEntry() {
  SRA0:  Float64LE,     # Right Ascension (radians)
  SDEC0: Float64LE,     # Declination (radians)
  ISP:   Bytes(2),      # Spectral type
  MAG:   Int16LE,       # Magnitude
  XRPM:  Float32LE,     # R.A. proper motion
  XDPM:  Float32LE      # Dec. proper motion
}

header: Byte[28]
stars: StarEntry[]
```

This works well when the file uses a single fixed entry format. For the full SAO format with variable-layout entries, see the [Getting Started](getting-started.md#making-it-flexible) guide.
