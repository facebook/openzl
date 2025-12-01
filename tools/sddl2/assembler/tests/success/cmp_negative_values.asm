# Test: CMP operations with negative values
# Stack: ... I64 I64 → ... I64
# -10 < -5 should produce I64(1)
push.i32 -10
push.i32 -5
cmp.lt
halt
