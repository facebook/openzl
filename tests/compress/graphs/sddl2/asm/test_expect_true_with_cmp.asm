# Test: expect_true with comparison (composability)
# Verifies that cmp.eq + expect_true works correctly

push.i32 42
push.i32 42
cmp.eq
expect_true
halt
