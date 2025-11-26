# Test: U16BE big-endian segment
# Input: 8 bytes (4 u16 values in big-endian)

push.tag 50
push.type.u16be
push.i32 4
segment.create_tagged
halt
