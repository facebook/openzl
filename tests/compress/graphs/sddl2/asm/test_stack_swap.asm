# Test: STACK swap operation
# Tests that stack.swap swaps the top two elements
# Pushes 10 and 20, swaps them (10, 20 -> 20, 10), then drops both
# Expected: No segments created, stack operations execute successfully

push.i32 10
push.i32 20
stack.swap
stack.drop
stack.drop
halt
