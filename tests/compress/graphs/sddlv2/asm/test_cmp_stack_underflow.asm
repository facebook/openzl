# Test: CMP with stack underflow
# Tests that cmp.eq with only 1 element on stack returns underflow error

push.i32 10
cmp.eq
halt
