; Test: segment creation with array type
; Creates a segment with array type U32LE[10]
; Total byte size should be: 10 elements * 4 bytes/element = 40 bytes
push.tag 100
push.type.u32le
push.i32 10
type.fixed_array
push.i32 10
segment.create_tagged
halt
