# Interpreter Test: expect_true with comparison operations
# Tests: Composability - using expect_true to validate comparison results
# Expected result: VM completes successfully (all comparisons are true)

# Test 1: cmp.eq + expect_true (42 == 42 -> true)
push.i32 42
push.i32 42
cmp.eq
expect_true

# Test 2: cmp.ne + expect_true (10 != 20 -> true)
push.i32 10
push.i32 20
cmp.ne
expect_true

# Test 3: cmp.lt + expect_true (5 < 10 -> true)
push.i32 5
push.i32 10
cmp.lt
expect_true

# Test 4: cmp.le + expect_true (10 <= 10 -> true)
push.i32 10
push.i32 10
cmp.le
expect_true

# Test 5: cmp.gt + expect_true (20 > 10 -> true)
push.i32 20
push.i32 10
cmp.gt
expect_true

# Test 6: cmp.ge + expect_true (10 >= 10 -> true)
push.i32 10
push.i32 10
cmp.ge
expect_true

halt
