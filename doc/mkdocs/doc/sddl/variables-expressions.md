# Variables and Expressions

*Chapter 9 - Computing derived values during parsing*

---

## Introduction

### Why Variables and Expressions?

### Use Cases

### Relationship to Instant-Parse

---

## The `var` Statement

### Basic Syntax

### Creating Variables

### Immutability

### Variable Naming

### Scope and Lifetime

#### Record Scope

#### Local Scope

#### Parameter Scope

---

## Referencing Parsed Fields

### Field References in Variables

### Nested Field Access

### When Field References Break Instant-Parse

### Safe Pattern: Using Parameters

---

## Expressions

### Expression Basics

### Expression Types

### Integer Arithmetic (64-bit Signed)

---

## Arithmetic Operators

### Addition (`+`)

### Subtraction (`-`)

### Multiplication (`*`)

### Division (`/`)

### Modulo (`%`)

### Unary Plus and Minus

---

## Bitwise Operators

### Bitwise AND (`&`)

### Bitwise OR (`|`)

### Bitwise XOR (`^`)

### Left Shift (`<<`)

### Right Shift (`>>`)

### Bitwise NOT (if supported)

---

## Comparison Operators

### Equality (`==`)

### Inequality (`!=`)

### Less Than (`<`)

### Less Than or Equal (`<=`)

### Greater Than (`>`)

### Greater Than or Equal (`>=`)

---

## Logical Operators

### Logical AND (`and`)

### Logical OR (`or`)

### Logical NOT (`not`)

---

## Operator Precedence

### Complete Precedence Table (C11)

### Precedence Examples

### Using Parentheses

---

## Switch Expressions

### Syntax and Structure

### Case Matching

#### Literal Values

#### Multiple Values

#### Ranges

#### Default Case

### Type Consistency

### Overlapping Cases (Error)

### Nested Switch Expressions

### Examples

---

## Standard Functions

### Function Overview

### Pure Functions

### Return Types

---

## Mathematical Functions

### `abs(x)` - Absolute Value

### `min(a, b)` - Minimum

### `max(a, b)` - Maximum

### `clamp(l, x, h)` - Bounded Value

### `sgn(x)` - Sign Function

---

## Range Functions

### `between(l, x, h)` - Range Check

---

## Arithmetic Helper Functions

### `ceil_div(x, d)` - Ceiling Division

### `align_up(x, a)` - Alignment Helper

---

## Size and Position Functions

### `sizeof(T())` - Static Size of Type

#### Syntax

#### Instant-Parse Implications

#### Examples

### `size(f)` - Parsed Byte Size of Field

#### Syntax

#### When Available

#### Instant-Parse Implications

### `current_position()` - Parser Position

#### Syntax

#### Breaks Instant-Parse

#### Use Cases

### `scope_remaining()` - Bytes to End of Scope

#### Syntax

#### Breaks Instant-Parse

#### Use Cases

---

## Error Handling in Expressions

### Overflow Behavior

### Division by Zero

### Format Error vs Data Error

### Detecting and Preventing Errors

---

## Variables and Instant-Parse

### When Variables Are Instant-Parse Safe

### When Variables Break Instant-Parse

### Patterns for Maintaining Instant-Parse

---

## Practical Examples

### Example 1: Computing Derived Sizes

### Example 2: Extracting Bit Fields

### Example 3: Conditional Logic with Variables

### Example 4: Using Switch Expressions

### Example 5: Size Calculations

### Example 6: Alignment Computations

---

## Common Patterns

### Pattern: Size Field Extraction

### Pattern: Flag Decomposition

### Pattern: Version-Based Calculation

### Pattern: Checksum Preparation

### Pattern: Offset Computation

---

## Variables in Validation

### Using Variables in `expect`

### Using Variables in `where`

### Computing Validation Thresholds

---

## Performance Considerations

### Compile-Time vs Runtime Evaluation

### Expression Complexity

### Function Call Overhead

### When Variables Matter for Performance

---

## Debugging Expressions

### Common Expression Errors

#### Type Mismatches

#### Undefined References

#### Scope Issues

#### Overflow

### Debugging Techniques

### Testing Expressions

---

## Advanced Topics

### Expression Evaluation Order

### Short-Circuit Evaluation (if applicable)

### Constant Folding

### Expression Optimization

---

## Limitations

### No User-Defined Functions

### No Floating-Point Expressions

### No String Operations

### Integer-Only Arithmetic

---

## Comparison with Other Languages

### C Expression Syntax

### Differences from C

### Intentional Limitations

---

## Summary

### Key Takeaways

### Expression Quick Reference

### Function Quick Reference

---

## Further Reading
