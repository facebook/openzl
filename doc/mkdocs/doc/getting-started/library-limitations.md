# Core Library Limitations
## Codec limitations
Compressing payloads with size > 500MB will result in undefined behavior. Such inputs must be chunked before compression.

## Requirements
The core library requires C11 to be compiled.

## Resource Usage
The OpenZL library is not optimized for memory usage. Typically, memory usage can be ~10x the payload that is getting compressed. Streaming is also not currently supported. Streaming is in active development and it is also expected for engine memory usage to be reduced significantly in future versions.
