# Structure array segment test
#
# Structure definition: {U8, I16LE, I32LE}
# Total structure size: 1 + 2 + 4 = 7 bytes
# Array of 10 instances = 70 bytes total
#
# This tests that SDDL2_parse() correctly:
# 1. Recognizes structure type segments
# 2. Splits structure into individual field arrays
# 3. Applies proper type conversion to each field
# 4. Routes each field to compression

push.tag 200

# Build structure type by pushing member types
push.type.u8        # Field 0: U8
push.type.i16le     # Field 1: I16LE
push.type.i32le     # Field 2: I32LE

# Create structure type from 3 members
push.i32 3
type.structure      # Stack: [tag, Type{STRUCTURE{U8, I16LE, I32LE}, width=7}]

# Create segment with 10 structure instances (70 bytes)
push.i32 10
segment.create_tagged

halt
