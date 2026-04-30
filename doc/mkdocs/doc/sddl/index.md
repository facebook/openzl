# SDDL — Simple Data Description Language

SDDL is a domain-specific language for describing binary file formats. It enables you to formally specify the structure of binary data so it can be efficiently processed by compression algorithms, transformation tools, and other downstream systems.

## Documentation Sections

### [Current Language](getting-started.md)

Documentation for the **currently supported** SDDL syntax — what you can use today with the SDDL2 compiler. Start here if you want to write SDDL descriptions that work now.

- [Getting Started](getting-started.md) — Your first SDDL description
- [Core Concepts](core-concepts.md) — Types, records, arrays, variables, and expressions
- [Conditional Fields](conditional-fields.md) — `when` blocks
- [Validation](validation.md) — `expect` statements
- [Examples](examples.md) — Complete format descriptions
- [Quick Reference](reference.md) — Supported syntax at a glance

### [North Star (v0.6)](north-star/index.md)

The **target specification** for SDDL — the full language design we're building toward. This includes features that are not yet implemented.

- [Full Documentation](north-star/index.md)
- [SDDL for LLMs](north-star/sddl-for-llm.md) — Complete v0.6 spec optimized for AI assistants

### [Legacy (SDDL1)](legacy/index.md)

Documentation for the **original SDDL language** (v1), which uses a different syntax and compiles to a CBOR-based runtime. This version is deprecated in favor of SDDL2.
