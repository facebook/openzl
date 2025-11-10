; Test: Array type segment (U32LE[10])
; Creates a segment with array type: 10 elements of U32LE
; Total size: 25 arrays × (10 × 4 bytes) = 1000 bytes
push.tag 100
push.type.u32le
push.i32 10
type.fixed_array
push.i32 25
segment.create_tagged
halt
