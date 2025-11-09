# Test: CMP with type mismatch
# Tests that cmp.eq with Tag values returns type mismatch error

push.tag 100
push.tag 200
cmp.eq
halt
