# SDDL2 — Simple Data Description Language, Redesigned

The current OpenZL release introduces **SDDL2**, a ground-up redesign of the Simple Data Description Language. SDDL2 replaces the original SDDL1 language with a cleaner syntax, a new compiler and runtime engine, and expanded capabilities for describing binary file formats.

## Instant Parse

SDDL2 introduces the concept of **instant-parse**: a record is instant-parse when all its field offsets, sizes, and layout can be computed from parameters and constants alone, without examining the data itself. When a record is instant-parse, the engine can jump directly to any field without scanning through preceding bytes, enabling zero-copy access and multi-GB/s throughput.

SDDL2 makes performance characteristics transparent: the language is designed so that instant-parse status is visible from the description itself, and the redesigned engine takes full advantage of it. On the Silesia/sao corpus, SDDL2 parses at ~2.4 GB/s, around 300x faster than SDDL1's ~8 MB/s (see `examples/sddl2/sao_full.sddl` and `examples/sddl/sao_full.oldv1.sddl`).

## Conditional Fields

An important feature of SDDL2 is **conditional fields** via `when` blocks, which allow fields to appear only when a condition is met. Conditions reference record parameters, making it possible to describe formats with optional sections, version-dependent layouts, and flag-controlled features, all within a single description whose layout is fully determined at instantiation time.

### Example: SAO Star Catalog

To see the difference in practice, here's how a star entry from the SAO catalog looks in both languages. Each entry has conditional fields that depend on header parameters: whether catalog numbers are present, which motion data is included, etc.

In **SDDL1**, there was no native conditional syntax. Instead you defined sub-records and included them as zero-or-one-element arrays gated by a boolean expression:

```sddl
StarEntry = (STNUM, MPROP, NMAG) {
  XnoSub = { XNO: Float32LE }
  PmSub  = { XRPM: Float32LE; XDPM: Float32LE }

  : XnoSub[STNUM > 0]
  SRA0 : Float64LE
  SDEC0: Float64LE
  ISP  : Byte[2]
  : Int16LE[abs(NMAG)]
  : PmSub[MPROP >= 1]
}
```

**SDDL2** replaces this with `when` blocks that read naturally:

```sddl
record StarEntry(STNUM, MPROP, NMAG) {
  when STNUM > 0 { XNO: Float32LE },
  SRA0:  Float64LE,
  SDEC0: Float64LE,
  ISP:   Bytes(2),
  when abs(NMAG) > 0 { MAG: Int16LE[abs(NMAG)] },
  when MPROP >= 1 {
    XRPM: Float32LE,
    XDPM: Float32LE
  }
}
```

The SDDL2 version is shorter, easier to read, and doesn't require defining throwaway sub-records just to get conditional inclusion.

## Developer Experience

Writing SDDL descriptions is now a lot more pleasant. The SDDL2 compiler includes a **semantic analysis** phase that goes beyond syntax checking to inspect the logical structure of a description. It catches errors like references to undefined fields, type mismatches in expressions, and parameter-arity mistakes. In SDDL1 these would only show up at runtime, or not at all. With SDDL2, you find out at compile time, before the description ever touches real data.

We've also added **VS Code syntax highlighting** for `.sddl` files, so keywords, field types, and conditional blocks are all color-coded as you write.
