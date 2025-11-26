# Test: STACK swap with underflow (only 1 element)
# Expected: SDDL2_STACK_UNDERFLOW
# Tests that stack.swap with only 1 element returns underflow error

push.i32 10
stack.swap
halt
