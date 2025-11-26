# Test: expect_true with zero value (failure)
# Expected: SDDL2_VALIDATION_FAILED
# Verifies that expect_true returns SDDL2_VALIDATION_FAILED with zero

push.i32 0
expect_true
halt
