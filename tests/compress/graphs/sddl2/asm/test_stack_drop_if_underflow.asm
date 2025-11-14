# Test: stack.drop_if with stack underflow (missing condition)
# Only pushes one value, then calls stack.drop_if
# Expected: SDDL2_STACK_UNDERFLOW error (needs 2 values: condition + value to drop)

push.i32 42
stack.drop_if
halt
