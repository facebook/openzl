# SDDL North Star — Version 0.6

!!! warning "North Star Specification"

    This section describes SDDL version 0.6 — the **target specification**. It represents the design goals and intended features of SDDL, serving as a "North Star" for the project's development.

    The current implementation does not yet support all features described here. For documentation of the currently supported syntax, see the [Current Language](../index.md) section.

## What is SDDL?

SDDL provides a clear, readable way to describe complex binary formats while maintaining strict guarantees about parsing performance. It's designed to bridge the gap between human-readable format specifications and high-performance binary processing.

## Key Features (Planned)

- **Explicit and Unambiguous**: No hidden defaults or implicit behaviors
- **Performance Visibility**: Clear distinction between instant-parse and scan-required layouts
- **Type Safety**: Rich type system with explicit endianness
- **Validation**: Built-in support for format constraints and data validation
- **Flexible Layouts**: Support for fixed-size, variable-size, and delimiter-based data

## Quick Example

```sddl
Record Header() = {
  magic: Bytes(4),
  version: Int16LE,
  flags: Int16LE
}

header: Header
expect header.magic == "DPKT"
expect header.version >= 1 and header.version <= 3
```

## Using SDDL with AI/LLM Assistants

**Want help writing SDDL specifications?** We provide a specialized teaching document designed for AI assistants:

**→ [SDDL for LLM (v0.6)](sddl-for-llm.md)** - Complete technical specification optimized for LLMs

You can provide this document to your AI assistant (ChatGPT, Claude, etc.) and ask it to help you create SDDL descriptions for your binary formats. The document contains the complete language specification in a format that LLMs can easily understand and apply.

## Documentation Structure

1. **[Introduction & Motivation](introduction.md)** - Understanding SDDL's purpose and benefits
2. **[Getting Started](getting-started.md)** - Your first SDDL file
3. **[Core Concepts](core-concepts.md)** - Types, records, and validation
4. **[Understanding Instant-Parse](instant-parse.md)** - Performance and layout determinism
5. **[Arrays and Collections](arrays-collections.md)** - Working with repeated data
6. **[Advanced Layout Control](alignment-padding.md)** - Alignment, padding, and memory optimization
7. **[Conditional & Variant Data](conditional-variant.md)** - Handling dynamic structures
8. **[Variables and Expressions](variables-expressions.md)** - Computing derived values
9. **[Real-World Formats](real-formats.md)** - Complete examples
10. **[Best Practices](best-practices.md)** - Guidelines for effective SDDL
11. **[Quick Reference](reference.md)** - Quick lookup guide
