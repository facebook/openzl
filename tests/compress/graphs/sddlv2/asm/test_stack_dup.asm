# Test: STACK dup operation
# Tests that stack.dup duplicates the top element
# Pushes 10, duplicates it (now: 10, 10), then drops both copies
# Expected: No segments created, stack operations execute successfully

push.i32 10
stack.dup
stack.drop
stack.drop
halt
