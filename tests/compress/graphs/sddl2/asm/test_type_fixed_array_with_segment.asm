; Test: segment creation with array type
; Creates a segment with array type U32LE[10]
; Total byte size: 1 element of Type{U32LE,10} = 1 × (10 × 4 bytes) = 40 bytes
push.tag 100
push.type.u32le
push.i32 10
type.fixed_array
push.i32 1
segment.create_tagged
halt
