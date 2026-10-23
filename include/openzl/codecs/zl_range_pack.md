## Range-Pack Manual
### Overview
The range-pack codec takes a single stream of integers and subtracts the minimum value from each element. The resulting values are then packed into a numeric stream with the minimum width in bytes required to represent the new range of values.

### Inputs
A single numeric input.

### Outputs
A single numeric output.

### Parameters
`ZL_RANGE_PACK_MAX_SUBTRACTED_PID`, or `ZL_Compressor_registerRangePackNode()`, bounds the subtracted value: the codec subtracts the smaller of this bound and the minimal element. With 0, it only narrows the values, which keeps their bit fields in place.

### Use Cases
This codec is useful for compressing numeric data that has a range of values that can be represented with a smaller number of bytes.
