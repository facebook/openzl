# Test: cmp.ne - not equal comparison (false case)
# Stack: ... I64 I64 → ... I64
# 42 != 42 should produce I64(0)
push.i32 42
push.i32 42
cmp.ne
halt
