# Test: STACK dup with underflow
# Expected: SDDL2_STACK_UNDERFLOW
# Tests that stack.dup on empty stack returns underflow error

stack.dup
halt
