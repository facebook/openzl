# Test: push.stack_depth combined with arithmetic
# Stack: [1, 2] -> [1, 2, 2, 10, 20]
# 2 * 10 = 20

push.i32 1
push.i32 2
push.stack_depth
push.i32 10
math.mul
push.i32 20
cmp.eq
expect_true
halt
