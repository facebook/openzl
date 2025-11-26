# Test: Single U8 typed segment
# Input: 4 bytes (U8 array)

push.tag 0
push.type.u8
push.i32 4
segment.create_tagged
halt
