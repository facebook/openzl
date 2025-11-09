# Test: cmp.eq - equal comparison (false case)
# Stack: ... I64 I64 → ... I64
# 10 == 20 should produce I64(0)
push.i32 10
push.i32 20
cmp.eq
halt
