# Conditional Fields

## Conditional Fields with `when`

Use `when` blocks to conditionally include fields based on a runtime value:

```sddl
Record Entry(has_extras) = {
  id: UInt32LE,
  value: Float64LE,
  when has_extras == 1 {
    extra1: UInt32LE,
    extra2: UInt32LE
  }
}
```

The `when` block is only executed (and its fields only consumed) if the condition evaluates to a non-zero value.

### Block Form

The currently supported syntax uses braces to group conditional fields:

```sddl
when condition {
  field1: Type1,
  field2: Type2
}
```

Multiple fields can appear inside a single `when` block.

### Conditions

Conditions can use any combination of variables, parameters, comparisons, and logical operators:

```sddl
Record FlexibleEntry(version, has_checksum) = {
  id: UInt32LE,
  data: UInt16LE,
  when version >= 2 {
    extended_data: UInt32LE
  }
  when has_checksum {
    checksum: UInt32LE
  }
}
```

### Nesting

`when` blocks can be nested:

```sddl
when version >= 2 {
  extended: UInt32LE,
  when has_extras == 1 {
    bonus: UInt16LE
  }
}
```

### Important Restrictions

- **Field references in conditions are not supported for member access.** You cannot reference fields consumed inside a `when` block from outside of it.
