# Test: cmp.ge - greater than or equal comparison (equal case)
# Stack: ... I64 I64 → ... I64
# 42 >= 42 should produce I64(1)
push.i32 42
push.i32 42
cmp.ge
halt
