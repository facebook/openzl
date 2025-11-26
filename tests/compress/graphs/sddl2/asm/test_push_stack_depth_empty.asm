# Test: push.stack_depth on empty stack
# Stack: [] -> [0]

push.stack_depth
push.i32 0
cmp.eq
expect_true
halt
