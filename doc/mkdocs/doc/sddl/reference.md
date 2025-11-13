# Language Reference

*Chapter 11 - Complete SDDL language specification*

---

## Overview

### Document Purpose

### Version Coverage

### Notation Conventions

---

## Lexical Structure

### Comments

### Identifiers

### Keywords

### Literals

#### Integer Literals

#### String Literals

#### Byte Array Literals

### Whitespace

### Statement Termination

#### Root-Level Statements

#### Block-Level Items

### Trailing Commas

---

## Type System

### Primitive Types

#### Integer Types

##### Signed Integers

##### Unsigned Integers

#### Floating-Point Types

##### IEEE Standard Floats

##### BFloat16

#### Raw Byte Types

### Composite Types

#### Records

#### Unions

#### Arrays

#### Enumerations

### Type Parameters

### Type Properties

#### Size

#### Alignment

#### Instant-Parse Status

---

## Record Definitions

### Syntax

### Parameters

### Fields

#### Field Names

#### Field Types

#### Throwaway Fields

### Inline Record Definitions

### Record Modifiers

#### `pad_to`

#### `pad_align`

---

## Union Definitions

### Syntax

### Dispatch Selector

### Cases

#### Literal Cases

#### Range Cases

#### Multiple Values per Case

#### Default Case

### Case Matching Rules

### Inline Union Definitions

---

## Enumeration Definitions

### Syntax

### Enum Values

### Enum Expressions

### Enum Member Access

---

## Fields and Statements

### Field Declarations

#### Basic Field Syntax

#### Parameterized Fields

### Conditional Fields

#### `when` Syntax

#### Condition Expressions

### Arrays

#### Fixed-Size Arrays

#### Multi-Dimensional Arrays

#### Auto-Sized Arrays

#### Structure-of-Arrays (SoA)

### Alignment

#### `align(n)` Modifier

### Scan Directive

#### When to Use `scan`

### Variable Declarations

#### `var` Syntax

#### Variable Scope

#### Immutability

### Validation Statements

#### `expect` Syntax

#### `where` Clauses

---

## Expressions

### Expression Types

### Operators

#### Arithmetic Operators

#### Bitwise Operators

#### Comparison Operators

#### Logical Operators

### Operator Precedence

### Integer Arithmetic

#### 64-bit Signed Integers

#### Overflow Behavior

#### Division by Zero

### Switch Expressions

#### Syntax

#### Case Matching

#### Type Consistency

---

## Standard Functions

### Mathematical Functions

#### `abs(x)`

#### `min(a, b)`

#### `max(a, b)`

#### `clamp(l, x, h)`

#### `sgn(x)`

### Range Functions

#### `between(l, x, h)`

### Arithmetic Helpers

#### `ceil_div(x, d)`

#### `align_up(x, a)`

### Size and Position Functions

#### `sizeof(T())`

#### `size(f)`

#### `current_position()`

#### `scope_remaining()`

---

## Annotations

### Annotation Syntax

### Standard Annotations

#### `@instant_parse`

#### `@chunk_size`

#### `@err_msg`

### Annotation Scopes

---

## Delimiter-Based Parsing

### `Bytes until` Syntax

### String Delimiters

### Binary Delimiters

### `include_delim` Option

### Missing Delimiter Behavior

---

## Instant-Parse Model

### Definition

### Instant-Parse Rules

#### For Records

#### For Unions

#### For Arrays

#### For Fields

### Transitivity

### Verification with `@instant_parse`

---

## Padding and Alignment

### `align(n)` Detailed Specification

### `pad_to n` Detailed Specification

### `pad_align n` Detailed Specification

### Order of Application

### Padding Bytes

---

## Structure-of-Arrays Layout

### SoA Syntax

### SoA Requirements

### SoA Restrictions

### Memory Layout

---

## Validation

### `expect` Statements

#### Evaluation Timing

#### Failure Behavior

#### Custom Error Messages

### `where` Clauses

#### Inline Validation

#### Evaluation Timing

---

## Scoping Rules

### Record Scope

### Union Scope

### Variable Scope

### Parameter Scope

### Field Visibility

---

## Error Model

### Error Categories

#### Format Errors

#### Data Errors

#### Instant-Parse Violations

### Error Reporting

### Error Messages

---

## Format Errors

### Definition

### Examples

#### Overlapping Union Cases

#### `pad_to` Too Small

#### Zero-Size Elements

#### Arithmetic Overflow

#### Division by Zero

---

## Data Errors

### Definition

### Examples

#### Missing Delimiter

#### Failed `expect` Check

#### Failed `where` Check

#### Unrecognized Union Case

---

## Instant-Parse Violations

### Definition

### Examples

### Diagnostic Information

---

## Type Catalog

### Complete Integer Type List

### Complete Float Type List

### Endianness Specifications

### Type Size Reference Table

---

## Function Reference

### Complete Function List

### Function Signatures

### Return Types

### Parameter Requirements

### Side Effects

---

## Annotation Reference

### Complete Annotation List

### Annotation Parameters

### Annotation Targets

### Annotation Semantics

---

## Operator Reference

### Complete Operator List

### Operator Precedence Table

### Operator Associativity

### Operator Semantics

---

## Keyword Reference

### Reserved Keywords

### Contextual Keywords

### Future Reserved Words

---

## Syntax Summary

### BNF Grammar

#### Type Definitions

#### Statements

#### Expressions

#### Fields

### Quick Reference Cards

---

## Endianness

### Explicit Endianness Requirement

### Little-Endian Types

### Big-Endian Types

### No Default Endianness

---

## Constraints and Limits

### Maximum Identifier Length

### Maximum Nesting Depth

### Maximum Array Dimensions

### Integer Value Range

### Implementation Limits

---

## Semantic Rules

### Name Uniqueness

#### Field Names

#### Variable Names

#### Type Names

### Type Compatibility

### Parameter Passing

### Implicit Conversions (None)

---

## Memory Model

### Sequential Parsing

### Field Ordering

### Padding Insertion

### Alignment Requirements

---

## Compilation Model

### Compilation Phases

### Type Checking

### Instant-Parse Analysis

### Code Generation

---

## Versioning

### Language Version

### Compatibility Guarantees

### Deprecation Policy

---

## Examples

### Minimal Example

### Comprehensive Example

### Edge Cases

---

## Appendix A: Complete Type Table

### All Integer Types with Sizes

### All Float Types with Sizes

### Endianness Mapping

---

## Appendix B: Complete Function Table

### Function Name, Parameters, Return Type, Description

---

## Appendix C: Complete Operator Table

### Operator Symbol, Precedence, Associativity, Description

---

## Appendix D: Complete Annotation Table

### Annotation Name, Parameters, Target, Description

---

## Appendix E: Complete Keyword Table

### Reserved Keywords

### Contextual Keywords

---

## Appendix F: Grammar

### Complete Formal Grammar

---

## Appendix G: ASCII Reference

### ASCII Codes for Common Delimiters

---

## Index
