# Test: push.stack_depth tracks stack elements
# Stack: [10, 20, 30] -> [10, 20, 30, 3]

push.i32 10
push.i32 20
push.i32 30
push.stack_depth
push.i32 3
cmp.eq
expect_true
halt
