# Test: MATH overflow error
# Expected: SDDL2_MATH_OVERFLOW
# Tests that overflow is properly detected and propagated
# INT64_MAX + 1 should overflow

push.i64 0x7FFFFFFFFFFFFFFF
push.i64 1
math.add
halt
