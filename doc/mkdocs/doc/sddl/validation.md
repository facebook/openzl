# Validation

## Validation with `expect`

Use `expect` to validate format constraints at parse time:

```sddl
header: Header
expect header.magic == 0x42494E44
expect header.version >= 1
expect header.version <= 3
expect header.entry_size == sizeof(DataEntry)
```

If an `expect` condition evaluates to zero (false), the SDDL engine reports a data error and parsing fails.

## Common Patterns

**Magic number validation:**
```sddl
header: FileHeader
expect header.signature == 0x4D42  # "BM" for BMP files
```

**Size consistency checks:**
```sddl
header: Header
expect header.entry_size == sizeof(Row)
```

**Range validation:**
```sddl
header: Header
expect header.count >= 0
expect header.count <= 1000000
```

**Combined conditions:**
```sddl
expect header.version >= 1 && header.version <= 3
```
