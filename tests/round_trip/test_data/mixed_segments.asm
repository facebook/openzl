# Test: Mixed type segments (U8, I32LE, BYTES)
# Input: 12 bytes total

# Segment 1: U8[4]
push.tag 1
push.type.u8
push.i32 4
segment.create_tagged

# Segment 2: I32LE[1]
push.tag 2
push.type.i32le
push.i32 1
segment.create_tagged

# Segment 3: BYTES[4]
push.i32 4
segment.create_unspecified

halt
