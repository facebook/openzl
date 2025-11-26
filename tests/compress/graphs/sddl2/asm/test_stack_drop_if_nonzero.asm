# Test: stack.drop_if with non-zero value as true
# Any non-zero value should be treated as true
# Pushes value 100, then condition 42 (non-zero = true)
# stack.drop_if should drop the 100
# Expected: Empty stack after drop_if

push.i32 100
push.i32 42
stack.drop_if
halt
