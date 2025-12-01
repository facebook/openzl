# Interpreter Test: expect_true with non-zero values (should succeed)
# Tests: expect_true operation succeeds with various non-zero values
# Expected result: VM completes successfully (no validation error)

# Test 1: expect_true with positive value
push.i32 1
expect_true

# Test 2: expect_true with large positive value
push.i32 123456
expect_true

# Test 3: expect_true with negative value
push.i32 -1
expect_true

# Test 4: expect_true with large negative value
push.i32 -999999
expect_true

halt
