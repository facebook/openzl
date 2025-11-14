# Test: Division by zero error
# Expected: SDDL2_DIV_ZERO
# Tests that math.div with zero divisor returns SDDL2_DIV_ZERO error

push.i32 10
push.i32 0
math.div
halt
