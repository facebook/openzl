## SDDL Syntax Reference

This section serves as an in-depth reference for the features of SDDL. For an introduction to SDDL, see the overall SDDL documentation.

!!! warning
    The SDDL Language is under active development. Its capabilities are expected to grow significantly. As part of that development, the syntax and semantics of existing features may change or break without warning.

An SDDL Description is a series of **Statements**. A statement is a newline or semicolon-terminated **Expression**. There are multiple kinds of **Expressions**:

### Fields

A **Field** is a tool to identify the type of data stored in a part of the input as well as to group appearances of that type of data in the input.

Fields are declared either by instantiating a built-in field or by composing one or more existing fields into a compound field. Input content is associated with fields via the **consume** operation.

#### Built-In Fields

The following table enumerates the predefined fields available in SDDL:

Name         | `ZL_Type` | Size | Sign? | Endian? | Returns
------------ | --------- | ---- | ----- | ------- | -------
`Byte`       | Serial    | 1    | No    | N/A     | `int64_t`
 `Int8`      | Numeric   | 1    | Yes   | N/A     | `int64_t`
`UInt8`      | Numeric   | 1    | No    | N/A     | `int64_t`
 `Int16LE`   | Numeric   | 2    | Yes   | Little  | `int64_t`
 `Int16BE`   | Numeric   | 2    | Yes   | Big     | `int64_t`
`UInt16LE`   | Numeric   | 2    | No    | Little  | `int64_t`
`UInt16BE`   | Numeric   | 2    | No    | Big     | `int64_t`
 `Int32LE`   | Numeric   | 4    | Yes   | Little  | `int64_t`
 `Int32BE`   | Numeric   | 4    | Yes   | Big     | `int64_t`
`UInt32LE`   | Numeric   | 4    | No    | Little  | `int64_t`
`UInt32BE`   | Numeric   | 4    | No    | Big     | `int64_t`
 `Int64LE`   | Numeric   | 8    | Yes   | Little  | `int64_t`
 `Int64BE`   | Numeric   | 8    | Yes   | Big     | `int64_t`
`UInt64LE`   | Numeric   | 8    | No    | Little  | `int64_t` (2s-complement)
`UInt64BE`   | Numeric   | 8    | No    | Big     | `int64_t` (2s-complement)
 `Float8`    | Numeric   | 1    | Yes   | N/A     | None
 `Float16LE` | Numeric   | 2    | Yes   | Little  | None
 `Float16BE` | Numeric   | 2    | Yes   | Big     | None
 `Float32LE` | Numeric   | 4    | Yes   | Little  | None
 `Float32BE` | Numeric   | 4    | Yes   | Big     | None
 `Float64LE` | Numeric   | 8    | Yes   | Little  | None
 `Float64BE` | Numeric   | 8    | Yes   | Big     | None
`BFloat8`    | Numeric   | 1    | Yes   | N/A     | None
`BFloat16LE` | Numeric   | 2    | Yes   | Little  | None
`BFloat16BE` | Numeric   | 2    | Yes   | Big     | None
`BFloat32LE` | Numeric   | 4    | Yes   | Little  | None
`BFloat32BE` | Numeric   | 4    | Yes   | Big     | None
`BFloat64LE` | Numeric   | 8    | Yes   | Little  | None
`BFloat64BE` | Numeric   | 8    | Yes   | Big     | None

A consumption operation invoked on one of these field types will evaluate to the value of the bytes consumed, interpreted according to the type of the field. E.g., if the next 4 bytes of the input are `"\x01\x23\x45\x67"`, the expression `result : UInt32BE` will store the value 0x01234567 in `result`. `"\xff"` consumed as a `Int8` will produce -1 where if it were instead consumed as `UInt8` or `Byte` it would evaluate to 255.

#### Compound Fields

##### Arrays

An array is constructed from a field and a length:

```
Foo = Byte
len = 1234

ArrayOfFooFields = Foo[len]
```

Consuming an **Array** consumes the inner field a number of times, equal to the provided length of the array.

!!! Note
    The field and length are evaluated when the array is declared, not when it is used. E.g.,

    ```
    Foo = Byte
    len = 42

    Arr = Foo[len]

    Foo = UInt32LE
    len = 10

    : Arr
    ```

    This will consume 42 bytes, not 10 32-bit integers.

##### Records

A **Record** is a sequential collection of **Fields**. A Record is declared by listing its member fields as a comma-separated list between curly braces:

```
Row = {
  Byte,
  Byte,
  UInt32LE[8],
}
```

A member field of type `T` in a record can be expressed in the following three ways:

```
{
  T,       # Bare field, implies the consumption of the field
  : T,     # An instruction to consume the field, equivalent to the previous
  var : T, # Consumption of the field, with the result assigned to a variable
}
```

Consuming a Record expands to an in-order consumption of its member fields.

The return value of consuming a Record is a scope object, which contains variables captured during consumption. Fields' values will be captured into this returned scope when they are expressed in the `variable : Field` syntax. These values can be retrieved from the returned scope using the `.` member access operator.

!!! example
    ```
    Header = {
      magic : UInt32LE,
      size : UInt32LE,
    }

    hdr : Header

    expect hdr.magic == 1234567890
    contents : Contents[hdr.size]
    ```

    This example demonstrates the declaration, consumption, and then use of values of member fields of a Record.

#### Field Instances

Each field declaration instantiates a new field. Different instances of a field, even when they have otherwise identical properties, may be treated differently by the SDDL engine.

Each use of a built-in field name is considered a declaration. E.g.,

```
Foo = {
  UInt64LE
  UInt64LE
  UInt64LE
  UInt64LE
}
```

is different from

```
U64 = UInt64LE

Foo = {
  U64
  U64
  U64
  U64
}
```

In the former, four different integer fields are declared, whereas in the latter only one is.

In the future, we intend for the SDDL engine to make intelligent decisions about how to map each fields to output streams. For the moment, though, each field instance is mechanically given its own output stream. This means that the two examples above produce different parses of the input:

In the former, the content consumed by `Foo` will be mapped to four different output streams, whereas in the latter it will all be sent to a single output stream.

### Numbers

Other than **Fields**, the other value that SDDL manipulates is **Numbers**.

!!! warning
    All numbers in SDDL are signed 64-bit integers.

    Smaller types are sign-extended into 64-bit width. Unsigned 64-bit fields are converted to signed 64-bit values via twos-complement conversion.

**Numbers** arise from integer literals that appear in the description, as the result of evaluating arithmetic expressions, or as the result of consuming a numeric type.

### Operations

Op        | Syntax        | Types         | Effect
--------- | ------------- | ------------- | ------
`expect`  | `expect <A>`  | I -> N        | Fails the run if `A` evaluates to 0.
`consume` | `[L] : <R>`   | V?, FL -> IS? | Consumes the field provided as `R`, stores the result into an optional variable `L`. The expression as a whole also evaluates to that result value.
`sizeof`  | `sizeof <A>`  | F -> I        | Evaluates to the size in bytes of the given field `A`. Fails the run if invoked on anything other than a static field.
`assign`  | `<L> = <R>`   | V, * -> *     | Stores the resolved value of the expression in `R` and stores it in the variable `L`. The assignment expression also evaluates as a whole to that resolved value.
`member`  | `<L>.<R>`     | S, V -> *     | Retrieves the value held by the variable `R` in the scope `L`. Cannot be used as the left-hand argument of assignment.
`bind`    | `<L>(<R...>)` | L, T -> L     | Binds the first `n` args of function `L` to the `n` comma-separated args `R`, returning a new function with `n` fewer unbound arguments.
`eq`      | `<L> == <R>`  | I, I -> I     | Evaluates to 1 if `L` and `R` are equal, 0 otherwise.
`neq`     | `<L> != <R>`  | I, I -> I     | Evaluates to 0 if `L` and `R` are equal, 1 otherwise.
`gt`      | `<L> > <R>`   | I, I -> I     | Evaluates to 1 if `L` is greater than `R`, 0 otherwise.
`ge`      | `<L> >= <R>`  | I, I -> I     | Evaluates to 1 if `L` is greater than or equal to `R`, 0 otherwise.
`lt`      | `<L> < <R>`   | I, I -> I     | Evaluates to 1 if `L` is less than `R`, 0 otherwise.
`le`      | `<L> <= <R>`  | I, I -> I     | Evaluates to 1 if `L` is less than or equal to `R`, 0 otherwise.
`neg`     | `- <A>`       | I -> I        | Negates `A`.
`add`     | `<L> + <R>`   | I, I -> I     | Evaluates to the sum of `L` and `R`.
`sub`     | `<L> - <R>`   | I, I -> I     | Evaluates to the difference of `L` and `R`.
`mul`     | `<L> * <R>`   | I, I -> I     | Evaluates to the product of `L` and `R`.
`div`     | `<L> / <R>`   | I, I -> I     | Evaluates to the quotient of `L` divided by `R`.
`mod`     | `<L> % <R>`   | I, I -> I     | Evaluates to the remainder of `L` divided by `R`.

In this table, the "Types" column denotes the signature of the operation, using the following abbreviations for the types of objects in SDDL:

- `F`: Field.
- `I`: Integer.
- `N`: Null.
- `V`: Variable name.
- `L`: Function (a "lambda").
- `S`: Scope.
- `T`: Tuple.

???+ note "Evaluation Order"
    Note that unlike C, which largely avoids defining the relative order in
    which different parts of an expression are evaluated (instead, only
    adding a limited number of sequencing points), SDDL has a defined order
    in which the parts of an expression are evaluated:

    **For binary operators, the left-hand side is always evaluated before the right-hand side.**

    This means that the behavior of valid expressions is always well-defined.
    E.g.:

    ```
    foo = :UInt32LE + 2 * :Byte
    ```

    must sequence the 32-bit int before the byte. Of course, it's probably
    better to avoid relying on this.

#### Consuming Fields

##### Consuming Atomic Fields

Consuming an atomic **Field** of size `N` has the following effects:

1. The next `N` bytes of the input stream, starting at the current cursor position, are associated with the field being consumed. This means they will be dispatched into the output stream associated with this field.

2. The cursor is advanced `N` bytes.

3. Those bytes are interpreted according to the field type (type, signedness, endianness), and the consumption operation evaluates to that value.

##### Consuming Compound Fields

Consuming a compound **Field** is recursively expanded to be a consumption of the leaf atomic fields that comprise the compound **Field**.

Currently, the consumption of a compound field does not produce a value.

### Variables

A **Variable** holds a value, the result of evaluating an **Expression**.

Variable names begin with an alphabetical character or underscore and contain any number of subsequent underscores or alphanumeric characters.

Variables are assigned to via either the `=` operator or as part of the `:` operator:

```
var = 2 + 2
expect var == 4

# consumes the Byte field, and stores the value read from the field into var.
var : Byte
```

Other than when it appears on the left-hand side of an assignment operation, referring to a variable in an expression resolves to the value it contains.

<!-- !!! danger
    TODO: document variable scoping.

    (For now, they're all global.) -->

#### Special Variables

The SDDL engine exposes some information to the description environment via
the following implicit variables:

Name   |  Type  | Value
-------|--------|------
`_rem` | Number | The number of bytes remaining in the input.

These variables cannot be assigned to.
