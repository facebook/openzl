# Test: stack.drop_if with negative value as true
# Negative values should also be treated as true (non-zero)
# Pushes value 50, then condition -1 (non-zero = true)
# stack.drop_if should drop the 50
# Expected: Empty stack after drop_if

push.i32 50
push.i32 -1
stack.drop_if
halt
