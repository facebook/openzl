# Wire and Materialized Format for ZL_Dict and ZL_DictBundle

## ZL_Dict
- 32-bit little-endian (magic = 0x5A4C4449 "ZLDI")
- 32-bit little-endian (nodeId = materializer node ID, or UINT32_MAX if unbound)
- 32-bit little-endian (dictSize = length of content in bytes)
- char array (content, dictSize bytes)

This is DIFFERENT from the in-memory representation, which contains, among other things, the hydrated dict object.

## ZL_DictBundle
- 32-bit little-endian (magic)
- SHA-256 (unique ID)
- 32-bit little-endian (number dicts)
- SHA-256 array (dict SHAs)
