# Test: stack.drop_if combined with comparison
# Practical use case: conditionally drop based on comparison result
# Pushes 10 and 5, compares if 10 > 5 (true), then drops value 99
# Expected: Empty stack (99 should be dropped)

push.i32 99
push.i32 10
push.i32 5
cmp.gt
stack.drop_if
halt
