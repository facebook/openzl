# Test: Division by zero error
# Tests that math.div with zero divisor returns SDDL2_DIV_ZERO error

push.i32 10
push.i32 0
math.div
halt
