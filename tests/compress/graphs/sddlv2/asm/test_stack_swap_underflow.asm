# Test: STACK swap with underflow (only 1 element)
# Tests that stack.swap with only 1 element returns underflow error

push.i32 10
stack.swap
halt
