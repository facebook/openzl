# Test: MATH with type mismatch
# Tests that math.add with Type values returns type mismatch error

push.type.u8
push.type.i32le
math.add
halt
