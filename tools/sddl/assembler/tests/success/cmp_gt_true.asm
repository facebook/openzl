# Test: cmp.gt - greater than comparison (true case)
# Stack: ... I64 I64 → ... I64
# 20 > 10 should produce I64(1)
push.i32 20
push.i32 10
cmp.gt
halt
