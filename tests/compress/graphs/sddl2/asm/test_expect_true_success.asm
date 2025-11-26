# Test: expect_true with non-zero values (success)
# Verifies that expect_true succeeds with non-zero I64 values

push.i32 1
expect_true

push.i32 42
expect_true

halt
