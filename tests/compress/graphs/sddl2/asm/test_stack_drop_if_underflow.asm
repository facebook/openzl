# Test: stack.drop_if with stack underflow (missing condition)
# Expected: SDDL2_STACK_UNDERFLOW
# Only pushes one value, then calls stack.drop_if
# (needs 2 values: condition + value to drop)

push.i32 42
stack.drop_if
halt
