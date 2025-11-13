# Protobuf Serialization

OpenZL supports serialization and deserialization of Protobuf messages directly into a compressed format.

All protobuf-related code is located in the `tools/protobuf` directory. You can experiment with this functionality using the `protobuf_cli` tool.

---

## Setup: Building the `protobuf_cli`

Before using the tool, you must build it from the root of the OpenZL repository.

```bash
mkdir -p build
cd build
cmake .. -DOPENZL_BUILD_PROTOBUF_TOOLS=ON
make
```

The `protobuf_cli` binary will be built at `build/tools/protobuf/protobuf_cli`.

**Note:** In the examples below, commands are shown as `protobuf_cli <command>` for readability. When running from the build directory, use `./tools/protobuf/protobuf_cli <command>`, or add it to your PATH for convenience.

---

## Core Concept: Schema Handling

The `protobuf_cli` can understand your `.proto` schemas in two ways.

* **1. Compiled Mode (Static):** The schema is "baked into" the `protobuf_cli` binary at compile time.

    * **Pro:** Slightly simpler command (no schema flags needed).
    * **Con:** Requires you to re-run `make` every time your schema changes.
    * **How to use:** Edit the `tools/protobuf/schema.proto` file *before* running `cmake` and `make`.

* **2. Dynamic Mode (Runtime):** You provide the schema as an argument every time you run a command.

    * **Pro:** Extremely flexible. No need to recompile to test new schemas or message types.
    * **Con:** Requires extra flags on every command (`--proto` or `--descriptor`). `--proto` may be slower than `--descriptor` as it requires parsing and compilation.
    * **How to use:** Pass schema flags (explained in examples below). See [Dynamic Schema Flags Reference](#dynamic-schema-flags-reference) for complete details.

You can use whichever mode best fits your workflow.

---

## CLI Usage: `protobuf_cli <command>`

The tool is organized into commands. The primary commands are `serialize` (for converting data), `train` (for creating custom compressors), and `benchmark` (for performance testing).

### Command: `serialize`

This command serializes, deserializes, or converts Protobuf messages between formats.

#### Common `serialize` Options

These flags work for *both* compiled and dynamic modes.

* `--input <file>`: The input file (required).
* `--output <file>`: The output file (optional). If not specified, the output filename is auto-generated from the input filename by changing the extension to match the output protocol.
* `--input-protocol <zl|proto|json>`: The format of the input file. (Default: `proto`)
* `--output-protocol <zl|proto|json>`: The format for the output file. (Default: `zl`)
* `--compressor <file>`: Use a custom-trained compressor file (see `train` command).
* `--check`: Verify that serialization round trip is correct.

#### `serialize` Examples

**Example 1: Compiled Mode (Serialize to ZL)**

This example assumes you already added your schema to `schema.proto` and recompiled.

```bash
# Serialize from Protobuf binary (.binpb) to OpenZL (.zl)
protobuf_cli serialize --input message.binpb --output message.zl
```

**Example 2: Compiled Mode (Deserialize ZL to JSON)**

This demonstrates format conversion.

```bash
# Deserialize from OpenZL (.zl) to JSON (.json)
protobuf_cli serialize \
  --input message.zl \
  --output message.json \
  --input-protocol zl \
  --output-protocol json
```

**Example 3: Dynamic Mode (using `.proto` file)**

This is the most common dynamic use case. You must specify the `.proto` file and the full message type.

```bash
protobuf_cli serialize \
  --proto schema.proto \
  --message-type com.example.MyMessage \
  --input input.binpb \
  --output output.zl
```

* If your `.proto` has imports, add search paths with `--proto-path <dir>`.

**Example 4: Dynamic Mode (using `.desc` file)**

For faster runtime performance, you can pre-compile your schema into a descriptor file:

```bash
protoc --descriptor_set_out=schema.desc --include_imports schema.proto
```

Then, use it with the CLI:

```bash
protobuf_cli serialize \
  --descriptor schema.desc \
  --message-type com.example.MyMessage \
  --input input.binpb \
  --output output.zl
```

---

### Command: `train`

This command trains a custom, optimized compressor from a set of sample messages.

#### Common `train` Options

* `--input <file|directory>`: Input file or directory of sample messages (required).
* `--output <file>`: Output file for the trained compressor (required).

#### `train` Examples

**Example 1: Compiled Mode**

This trains using the schema baked into the binary.

```bash
# Input can be a single file or a directory of files
protobuf_cli train --input training_data/ --output trained.zlc
```

**Example 2: Dynamic Mode**

This trains using a schema provided at runtime. Just like `serialize`, you must provide the schema and message type.

```bash
protobuf_cli train \
  --proto schema.proto \
  --message-type com.example.MyMessage \
  --input training_data/ \
  --output custom_compressor.zlc
```

You can then use this `custom_compressor.zlc` file with the `serialize` command:

```bash
protobuf_cli serialize \
  --proto schema.proto \
  --message-type com.example.MyMessage \
  --input new_message.binpb \
  --output new_message.zl \
  --compressor custom_compressor.zlc
```

---

### Command: `benchmark`

This command runs a performance benchmark for compression and decompression. It follows the same schema rules as `serialize` and `train`.

#### Common `benchmark` Options

* `--input <file|directory>`: Input file or directory of sample messages to benchmark (required).
* `--num-iters <number>`: Number of iterations to run for each file (optional, default: 10).

#### `benchmark` Examples

**Example 1: Compiled Mode**

```bash
protobuf_cli benchmark --input samples/ --num-iters 100
```

**Example 2: Dynamic Mode**

```bash
protobuf_cli benchmark \
  --proto schema.proto \
  --message-type com.example.MyMessage \
  --input samples/ \
  --num-iters 100
```

---

## Dynamic Schema Flags Reference

When using dynamic mode, these flags are required:

* `--proto <file>`: Load schema from a `.proto` source file (requires protobuf compiler libraries).
* `--descriptor <file>`: Load schema from a pre-compiled `.desc` descriptor file (faster, recommended for production).
* `--message-type <type>`: [Fully qualified message type name](https://protobuf.dev/programming-guides/proto3/#packages), including the package. For example, if your `.proto` file has `package com.example;` and defines `message MyMessage`, use `com.example.MyMessage`. Required with `--proto` or `--descriptor`.
* `--proto-path <directory>`: Directory to search for `.proto` imports.

**Note:** You must use either `--proto` or `--descriptor`, but not both.
