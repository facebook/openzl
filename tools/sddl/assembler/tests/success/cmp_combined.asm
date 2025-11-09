# Test: Multiple CMP operations in sequence
# Stack: ... I64 I64 → ... I64
push.i32 100
push.i32 50
cmp.gt
push.i32 25
push.i32 75
cmp.lt
cmp.eq
halt
