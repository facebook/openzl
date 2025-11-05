# Test: cmp.ge - greater than or equal comparison (true case)
# Stack: ... I64 I64 → ... I64
# 30 >= 10 should produce I64(1)
push.i32 30
push.i32 10
cmp.ge
halt
