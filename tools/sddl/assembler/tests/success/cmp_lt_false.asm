# Test: cmp.lt - less than comparison (false case)
# Stack: ... I64 I64 → ... I64
# 20 < 10 should produce I64(0)
push.i32 20
push.i32 10
cmp.lt
halt
