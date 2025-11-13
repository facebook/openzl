# Advanced Layout Control

*Chapter 6 - Alignment, padding, and memory optimization*

---

## Introduction to Memory Layout

### Why Layout Matters

### Hardware Alignment Requirements

### Cache Line Considerations

### Impact on Compression

---

## Alignment with `align(n)`

### Basic Alignment Syntax

### Aligning Types

### Aligning Fields

### Impact on Record Size

### Alignment in Arrays

### Inter-Element Padding

### Alignment and Instant-Parse

---

## Padding Records

### `pad_to n` - Exact Size Enforcement

#### Syntax and Semantics

#### When to Use `pad_to`

#### Error Conditions

#### Examples

### `pad_align n` - Rounding to Multiples

#### Syntax and Semantics

#### When to Use `pad_align`

#### Examples

### Combining `pad_to` and `pad_align`

#### Order of Operations

#### Use Cases

---

## Structure-of-Arrays (SoA) Layout

### What is SoA?

### AoS vs SoA Comparison

### Syntax: `soa Type[count]`

### When to Use SoA

#### Columnar Data Access

#### SIMD Operations

#### Compression Benefits

#### Analytics Workloads

### SoA Requirements and Restrictions

#### Element Type Must Be Instant-Parse

#### No `var` Creation or `expect` Statements

#### No Conditional Fields

### Auto-Sized SoA Arrays

### SoA Memory Layout Visualization

### Performance Implications

---

## Memory Layout Patterns

### Contiguous Allocation

### Interleaved vs Separated

### Hot/Cold Data Separation

### Cache-Friendly Layouts

---

## Visual Examples

### Hex Dump Walkthrough

### Memory Layout Diagrams

### Alignment Gap Visualization

### AoS vs SoA Side-by-Side

---

## Practical Examples

### Example 1: Aligning for SIMD

### Example 2: Fixed-Size Records with Padding

### Example 3: SoA for Particle Data

### Example 4: Cache Line Optimization

### Example 5: GPU-Friendly Layout

---

## Performance Optimization

### Choosing Between AoS and SoA

### Alignment for Specific Architectures

### Minimizing Padding Overhead

### Layout for Vectorization

### Trade-offs: Size vs Speed

---

## Common Pitfalls

### Pitfall 1: Unnecessary Alignment

### Pitfall 2: Misunderstanding `pad_to` vs `pad_align`

### Pitfall 3: Breaking Instant-Parse with SoA

### Pitfall 4: Over-Padding

### Pitfall 5: Architecture-Specific Assumptions

---

## Layout and Compression

### How Layout Affects Compression Ratio

### Optimal Layouts for Different Codecs

### SoA Benefits for Compression

---

## Platform Considerations

### x86/x86_64 Alignment Rules

### ARM Alignment Rules

### GPU Memory Layout

### Cross-Platform Portability

---

## Advanced Topics

### Alignment Propagation in Nested Structures

### Custom Padding Strategies

### Dynamic Alignment (Runtime)

### Alignment and Memory Mapping

---

## Debugging Layout Issues

### Inspecting Generated Layouts

### Measuring Padding Overhead

### Validating Alignment

### Performance Profiling

---

## Summary

### Key Takeaways

### Decision Matrix: When to Use Each Feature

### Quick Reference

---

## Further Reading
