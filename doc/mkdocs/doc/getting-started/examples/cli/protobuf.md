# Protobuf Serialization
OpenZL supports serialization and deserialization of Protobuf messages directly into a compressed format.

## Experimenting with Protobuf Serialization
All protobuf-related code is located in the `tools/protobuf` directory. You can experiment with OpenZL's Protobuf support using the `protobuf_cli` tool.

### Adding Your Schema
The first step is to add your Protobuf schema to the `tools/protobuf/schema.proto` file. You should replace the existing schema with your own.

### Building the Protobuf CLI
Once you have added your Protobuf schema, you should re-build the `protobuf_cli` tool. This can be done by running the following commands from the root of the OpenZL repository:
```
cmake -DOPENZL_BUILD_PROTOBUF_TOOLS=ON
make
```

### Using the Protobuf CLI
The `protobuf_cli` tool can be used to serialize Protobuf messages. You can serialize a Protobuf message by running the following command:
```
protobuf_cli serialize --input <input_file> --output <output_file>
```

By default, the input is expected to be a proto serialized message and the output will be in the OpenZL format. You can change these defaults using the `--input-protocol` and `--output-protocol` flags. The supported protocols are `zl`, `proto`, and `json`.

For example, if your input file is in the OpenZL format and you want your output to be protobuf, you can run the following command:
```
protobuf_cli serialize --input <input_file> --output <output_file> --input-protocol zl --output-protocol proto
```
