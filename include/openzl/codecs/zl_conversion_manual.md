## Conversion Manual
### Overview
The 'conversion' codecs are a family of codecs that convert the type of the input from its original type to the target type to convert to. There is a conversion codec from the serial type to any other type, and a conversion codec from any other type to the serial type.
### Inputs
#### SerialToToken, SerialToNumeric, SerialToString, TokenToSerial, NumToSerial
An input of type serial, struct, numeric.

#### StringToSerial
- An input of type string
- A numeric input containing the lengths of the strings.

### Outputs
#### SerialToToken, SerialToNumeric, StringToSerial, TokenToSerial, NumToSerial
A single output of the specified type that is compatible - serial, struct, numeric. The output contains the converted data in the output format. See the spec for more details on the typed format.

## SerialToString
StringToSerial has 2 outputs, one of type serial containing a serial representation of the string data, the second output contains a numeric output containing sizes of the strings in the input.

### Use Cases
Used to invoke codecs with different underlying types from the received type. One common example is entropy codecs which only take serial inputs. When compressing numeric data, it is necessary to convert to serial then run the entropy codecs.
