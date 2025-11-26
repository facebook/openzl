# Test: stack.drop_if with true condition (non-zero)
# Pushes value 42, then condition 1 (true)
# stack.drop_if should drop the 42
# Expected: Empty stack after drop_if

push.i32 42
push.i32 1
stack.drop_if
halt
