# Test: MATH with stack underflow
# Tests that math.add with only 1 element on stack returns underflow error

push.i32 10
math.add
halt
