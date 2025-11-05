# Test: cmp.ne - not equal comparison (true case)
# Stack: ... I64 I64 → ... I64
# 10 != 20 should produce I64(1)
push.i32 10
push.i32 20
cmp.ne
halt
