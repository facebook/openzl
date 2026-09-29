---
name: openzl-codec-design
description: Design patterns and requirements for OpenZL codecs (located under `src/openzl/codecs/`). **USE AUOMATICALLY** when creating, modifying, or reviewing codecs in OpenZL.
---

# Creating or Modifying an OpenZL Codec

## Workflow for Creating a New Codec

If the user provides an input to this skill, interpret it as an **informal specification** of the codec they want to create. Before writing any code:

1. **Ask clarifying questions** about the format. Resolve ambiguities around input/output types, element widths, codec header layout, edge cases, error conditions, and whether the codec is public or private.
2. **Write `spec.md` first.** ALWAYS generate the decoder wire format specification (`spec.md`) before any other file. Follow the conventions of existing specs in `src/openzl/codecs/` (inputs, codec header, decoding algorithm, outputs).
3. **Ask the user to verify the spec.** Do NOT proceed with implementation until the user has reviewed and approved the `spec.md`. The spec is the contract — all code flows from it.

## Security

### Decoder MUST Be Safe to Malicious Inputs

The decoder processes untrusted data from compressed frames. All assumptions MUST be validated, especially:
- Bounds on sizes, counts, and offsets read from the frame header
- Element widths and stream types
- Buffer sizes before writing

Never trust values from the compressed stream without verification. A malicious frame must not cause crashes, out-of-bounds access, or undefined behavior.

### Encoder MUST Be Safe to Malicious Inputs

The encoder is processing data that the user provides. Unless explicitly stated otherwise all assumptions MUST be validated. Typcially, this is less of an issue on the encoder side, but any assumptions the encoder makes about the input data (e.g. it doesn't contain the value 0) must be validated, otherwise the data could be corrupted.

## Requirements

### General

- Codecs must work on big & little endian machines, and must not depend on endian-ness.
- Prefer to use helpers that already exist in `src/openzl/shared/`, rather than re-implementing them.
   - Especially `openzl/shared/mem.h`, `openzl/shared/bits.h`, and `openzl/shared/utils.h`.
- All code MUST be portable across platforms. E.g. both ARM and x86-64, 32- and 64-bit systems, and both little- and big-endian.

### Bumping ZL_MAX_FORMAT_VERSION

When adding a new codec, or making a breaking change to a codec that requires bumping the format version, we need to make sure the development branch bumps the format version.
If not adding a codec or making a format breaking change to a codec, then you can skip this section.

Determine the production max format version from the `ZL_MAX_FORMAT_VERSION` macro in `fbcode/openzl/prod/include/openzl/zl_version.h`, call it `$prod_max_format_version`.
Determine the development max format version from the `ZL_MAX_FORMAT_VERSION` macro in `fbcode/openzl/dev/include/openzl/zl_version.h`, call it `$dev_max_format_version`.
If `$dev_max_format_version == $prod_max_format_version`, then the development `ZL_MAX_FORMAT_VERSION` in `fbcode/openzl/dev/include/openzl/zl_version.h` must be bumped.
This needs to be done before hooking up the encoder and decoder registry so that the new max format version is used during registration.

### Format Versioning

Codecs MUST preserve forward and backward compatibility with all supported format versions from `ZL_MIN_FORMAT_VERSION` to `ZL_MAX_FORMAT_VERSION`.
Codecs MAY change their format, with very careful consideration, but they MUST do it in way that preserves compatibility:

- `ZL_MAX_FORMAT_VERSION` must be bumped in the dev branch (see above)
- The `spec.md` file MUST be updated to reflect the variation based on the format version
- The encoder MUST check the format version with `ZL_Encoder_getCParam(eictx, ZL_CParam_formatVersion)` and only emit the new format for the latest format version
- The encoder MUST maintain the ability to emit the older format versions down to the minimum format version the codec supports
- The decoder MUST check the format version with `DI_getFrameFormatVersion(dictx)` and correctly interpret the encoded data based on the format version

While codecs are allowed to change their format, this is something that should only be done with extreme care, and done very rarely.
It should NEVER be done on a whim to fix a small issue.
Format versions will be supported for years, so every change compounds the maintainence burden of the code for years.

### Encoder Parameters

Parameters MUST be validated and cannot be trusted to match a particular format.
Parameters can come from a serialized compressor, which means that an attacker could control the serialized compressor, so all assumptions about parameters MUST be validated.
E.g. if a parameter is supposed to be an 8-byte uint64_t, then the size of the parameter MUST be validated to be equal to 8.

## Directory Layout

Each codec lives in `src/openzl/codecs/{codec_name}/`. A typical codec has these files:

```
{codec_name}/
  encode_{codec}_binding.h     # Encoder binding API + registration macro (EI_CODEC)
  encode_{codec}_binding.c     # Encoder binding implementation
  encode_{codec}_kernel.h      # Encoder kernel API (transportable, no openzl deps)
  encode_{codec}_kernel.c      # Encoder kernel implementation
  decode_{codec}_binding.h     # Decoder binding API + registration macro (DI_CODEC)
  decode_{codec}_binding.c     # Decoder binding implementation
  decode_{codec}_kernel.h      # Decoder kernel API (transportable, no openzl deps)
  decode_{codec}_kernel.c      # Decoder kernel implementation
  graph_{codec}.h              # [optional] Graph descriptor (I/O stream types)
  spec.md                      # [optional] Human-readable decoder wire format spec
```

### Kernel vs Binding Split

**Kernel** = pure algorithm. Minimal deps (C standard library, `openzl/shared/`). No memory allocation. Must be independent of the OpenZL engine and transportable to other contexts. Publishes its own lean interface.

**Binding** = glue between kernel and OpenZL engine. Implements `ZL_Encoder`/`ZL_Decoder` interfaces. Handles pre-condition checks, error reporting, memory allocation, and kernel orchestration.

Simple codecs may skip the kernel and put everything in the binding. Complex codecs may have multiple kernel variants.

See `README.md` for the full conventions.

### Graph Descriptors

Define I/O stream types. Common reusable graphs in `src/openzl/codecs/common/graph_pipe.h`:
- `NUMPIPE_GRAPH(id)` — 1 numeric in, 1 numeric out (used by zigzag, delta, divide_by)
- `PIPE_GRAPH(id)` — 1 serial in, 1 serial out (used by lz4, zstd, rolz)

### spec.md

A human-readable specification of the decoder wire format: inputs, codec header format, decoding algorithm, and outputs. Written from the decoder's perspective.

## Encoder Binding Pattern

The encoder binding header declares the encode function and a registration macro:

```c
ZL_Report EI_{codec}(ZL_Encoder* eictx, const ZL_Input* ins[], size_t nbIns);

#define EI_CODEC(id)                    \
    { .gd          = GRAPH_MACRO(id),   \
      .transform_f = EI_{codec},        \
      .name        = "!zl.{codec}" }
```

## Decoder Binding Pattern

```c
ZL_Report DI_{codec}(ZL_Decoder* dictx, const ZL_Input* in[]);

#define DI_CODEC(id) { .transform_f = DI_{codec}, .name = "!zl.{codec}" }
```

## Adding the Wire Format ID

In `src/openzl/common/wire_format.h`, add a new entry to the `ZL_StandardTransformID` enum before `ZL_StandardTransformID_end`. Use an available slot (look for gaps marked "available"). These IDs are serialized into compressed frames and must remain **stable forever**.

## Adding the Encoder Node ID

- **Public codecs**: Add to `ZL_StandardNodeID` in `include/openzl/zl_nodes.h` (before `ZL_StandardNodeID_public_end`)
- **Private codecs**: Add to `ZL_PrivateStandardNodeID` in `src/openzl/compress/private_nodes.h` (before `ZL_PrivateStandardNodeID_end`)

## Public C Header (`include/openzl/codecs/`)

For public codecs, create `include/openzl/codecs/zl_{codec}.h`:

1. Include `openzl/zl_nodes.h` (for nodes) or `openzl/zl_graphs.h` (for graphs)
2. Wrap in `extern "C"` guards
3. Add a comment block describing the codec's inputs, outputs, and behavior
4. Define the public ID macro:
   - Nodes: `#define ZL_NODE_{CODEC} ZL_MAKE_NODE_ID(ZL_StandardNodeID_{codec})`
   - Graphs: `#define ZL_GRAPH_{CODEC} ZL_MAKE_GRAPH_ID(ZL_StandardGraphID_{codec})`
5. Optionally define parameter IDs (`ZL_{CODEC}_PID`) and C API functions for parameterized codecs

Then add `#include "openzl/codecs/zl_{codec}.h" // IWYU pragma: export` to `include/openzl/zl_public_nodes.h`.

See existing headers in `include/openzl/codecs/` for examples (e.g., `zl_zigzag.h` for simple, `zl_partition.h` for complex).

## C++ Binding (`cpp/include/openzl/cpp/codecs/`)

Create `cpp/include/openzl/cpp/codecs/{Codec}.hpp`:

1. Include the corresponding C header (`openzl/codecs/zl_{codec}.h`)
2. Include C++ framework headers: `Compressor.hpp` and either `Node.hpp` or `Graph.hpp`, plus `Metadata.hpp`
3. Define the class in `namespace openzl::nodes` (or `openzl::graphs`):
   - For simple 1-in 1-out nodes: inherit `SimplePipeNode<{Codec}>`
   - For simple graphs: inherit `SimpleGraph<{Codec}>`
   - For complex codecs: inherit `Node` or `Graph` directly
4. Declare `static constexpr NodeID node = ZL_NODE_{CODEC};` (or `GraphID graph = ZL_GRAPH_{CODEC};`)
5. Declare `static constexpr auto metadata = NodeMetadata<nInputs, nSingletonOutputs>{...};` with input/output types, names, and description
6. For parameterized codecs: add constructor taking config, override `parameters()`

Then add `#include "openzl/cpp/codecs/{Codec}.hpp" // IWYU pragma: export` to `cpp/include/openzl/cpp/Codecs.hpp`.

See `cpp/include/openzl/cpp/codecs/Zigzag.hpp` for a simple example, `cpp/include/openzl/cpp/codecs/Partition.hpp` for a complex one.

## Hooking Up the Registries

### Encoder Registry (`src/openzl/codecs/encoder_registry.c`)

1. Add `#include "openzl/codecs/{codec}/encode_{codec}_binding.h"`
2. Add entry to `ER_standardNodes[]`:
```c
// REGISTER_TRANSFORM(NodeID, TransformID, minFormatVer, minLibraryVer, EI_MACRO)
REGISTER_TRANSFORM(ZL_StandardNodeID_{codec}, ZL_StandardTransformID_{codec}, 26, 202, EI_CODEC),
```

NOTE: Hard-code the **current numeric values** of `ZL_MAX_FORMAT_VERSION` and `ZL_LIBRARY_VERSION_NUMBER` (e.g., `26, 202`) — do NOT pass the macros themselves. These pin the minimum format/library version required to use this codec; using the macros would silently widen support every time they bump (see `ACR_explicit_format_version_in_registrations`).

### Decoder Registry (`src/openzl/codecs/decoder_registry.c`)

1. Add `#include "openzl/codecs/{codec}/decode_{codec}_binding.h"`
2. Add entry to `SDecoders_array[]` using the appropriate macro:
   - `REGISTER_TTRANSFORM_G` — fixed typed I/O (most common)
   - `REGISTER_VOTRANSFORM_G` — variable number of outputs
   - `REGISTER_MITRANSFORM_G` — variable number of inputs
```c
REGISTER_TTRANSFORM_G(ZL_StandardTransformID_{codec}, ZL_MAX_FORMAT_VERSION, DI_CODEC, GRAPH_MACRO),
```

NOTE: Do not use the macro `ZL_MAX_FORMAT_VERSION`, use the current value of that macro! This value tells OpenZL the minimum format version that is required to use this codec, which for new codecs is the current maximum format version.

## Testing

### Adding a Test Component

See `tests/registry/README.md` and `tests/registry/OpenZLComponents.h` for instructions.

Steps:
1. Add enum value to `OpenZLComponentID` in `tests/registry/OpenZLComponents.h` (before `NumComponents`)
2. Add factory declaration `make${Component}Component()` in the `components` namespace
3. Add case to the `makeOpenZLComponent()` switch statement
4. Create `tests/registry/components/${Component}.cpp` implementing `OpenZLComponent`

Required overrides: `name()`, `minFormatVersion()`, `predefinedInputs()` (edge cases).
Optional overrides: `predefinedNodes()`/`predefinedGraphs()`, `generateInputs()`, `generateNodes()`/`generateGraphs()`.

Prefer using the C++ bindings (e.g., `nodes::Zigzag{}`, `graphs::Bitpack{}`) to create nodes and graphs in the test component rather than raw C API calls. Include from `openzl/cpp/codecs/{Codec}.hpp` and use `parameterize(compressor)` to build nodes/graphs.

See `tests/registry/components/Zigzag.cpp` for a simple example.

### Running Component Tests

```bash
buck test fbcode//openzl/dev/tests:integrationtest
```

### Running Component Fuzzers

Run fuzzers only after all tests pass. Run all 3 in parallel with a 10-minute timeout:

```bash
arc lionhead bundle run Zstrong_OpenZLComponentFuzzer_FuzzRoundTrip --run-duration-secs 600
arc lionhead bundle run Zstrong_OpenZLComponentFuzzer_FuzzCompress --run-duration-secs 600
arc lionhead bundle run Zstrong_OpenZLComponentFuzzer_FuzzDecompress --run-duration-secs 600
```
