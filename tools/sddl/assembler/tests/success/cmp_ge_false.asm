# Test: cmp.ge - greater than or equal comparison (false case)
# Stack: ... I64 I64 → ... I64
# 5 >= 50 should produce I64(0)
push.i32 5
push.i32 50
cmp.ge
halt
