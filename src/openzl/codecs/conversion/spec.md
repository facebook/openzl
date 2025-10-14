## Conversion Decoder Specification
### Inputs
#### SerialToToken, SerialToNumeric, StringToSerial, TokenToSerial, NumToSerial
An input of type serial, struct, numeric.
#### SerialToString
- An input of type string
- A numeric input containing the lengths of the strings.

### Codec Header
Codecs NumToSerial and StructToSerial contain a variant encoded value in the header. This represents the element width.

### Decoding
Codecs exist for conversion between serial and any other type, as well as the reverse. The input is decoded such that the byte representation of the input is the same as in the output. The element width is specified in the header.

#### Numeric Input Byte Representation
A numeric input's byte representation is determined by its element width. With width $n$, each value in the input is written into the $n$ bytes in little endian format.

#### Struct Input Byte Representation
A struct input with element width $n$ is written so each value in the input is written into the $n$ bytes in little endian format.

#### String Input Byte Representation
A string input has byte representation where each string is consecutively written to the same buffer. It must be coupled with a numeric input containing the lengths of the strings in 4-byte unsigned LE integer format. This will be used to determine where to split the input for StringToSerial codec and should be generated for SerialToString codec to recreate the original regenerated input.

### Outputs
#### SerialToToken, SerialToNumeric, SerialToString, TokenToSerial, NumToSerial
A single output of the specified type that is compatible - serial, struct, numeric. The output contains the converted data in the output format.

#### StringToSerial
StringToSerial has 2 outputs, one of type serial containing a serial representation of the string data, the second output contains a numeric output containing sizes of the strings in the input.
