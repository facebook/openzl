# SDDL - Simple Data Description Language

SDDL is a domain-specific language for describing binary file formats. It enables you to formally specify the structure of binary data so it can be efficiently processed by compression algorithms, transformation tools, and other downstream systems.

## What is SDDL?

SDDL provides a clear, readable way to describe complex binary formats while maintaining strict guarantees about parsing performance. It's designed to bridge the gap between human-readable format specifications and high-performance binary processing.

## Key Features

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

## Getting Started

New to SDDL? Start with the [Introduction & Motivation](introduction.md) to understand why SDDL exists and how it can help you. Then follow the [Getting Started](getting-started.md) guide to create your first SDDL file.

## Documentation Structure

1. **[Introduction & Motivation](introduction.md)** - Understanding SDDL's purpose and benefits
2. **[Getting Started](getting-started.md)** - Your first SDDL file
3. **[Core Concepts](core-concepts.md)** - Types, records, and validation
4. **[Arrays and Collections](arrays-collections.md)** - Working with repeated data
5. **[Understanding Instant-Parse](instant-parse.md)** - Performance and layout determinism
6. **[Advanced Layout Control](layout-control.md)** - Alignment, padding, and memory optimization
7. **[Conditional & Variant Data](conditional-variant.md)** - Handling dynamic structures
8. **[Working with Real Formats](real-formats.md)** - Text protocols and mixed formats
9. **[Variables and Expressions](variables-expressions.md)** - Computing derived values
10. **[Best Practices](best-practices.md)** - Guidelines for effective SDDL
11. **[Language Reference](reference.md)** - Complete language specification

## Community and Support

- **GitHub**: [facebook/openzl](https://github.com/facebook/openzl)
- **Issues**: Report bugs or request features on GitHub

## Version

This documentation describes SDDL version 0.6.
