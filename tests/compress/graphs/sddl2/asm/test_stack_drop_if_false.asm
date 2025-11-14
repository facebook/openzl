# Test: stack.drop_if with false condition (zero)
# Pushes value 42, then condition 0 (false)
# stack.drop_if should NOT drop the 42 (value remains)
# Expected: Stack contains one I64 value (42)

push.i32 42
push.i32 0
stack.drop_if
stack.drop
halt
