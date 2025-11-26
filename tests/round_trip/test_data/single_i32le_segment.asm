# Test: Single I32LE typed segment
# Input: 4 bytes (1 i32 in little-endian)

push.tag 100
push.type.i32le
push.i32 1
segment.create_tagged
halt
