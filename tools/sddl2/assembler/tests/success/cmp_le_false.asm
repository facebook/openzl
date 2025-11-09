# Test: cmp.le - less than or equal comparison (false case)
# Stack: ... I64 I64 → ... I64
# 30 <= 10 should produce I64(0)
push.i32 30
push.i32 10
cmp.le
halt
