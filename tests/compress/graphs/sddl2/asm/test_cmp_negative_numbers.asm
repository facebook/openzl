# Test: Comparison operations with negative numbers
# Tests signed comparison behavior with negative values
# Tests: lt (-10 < 5 -> 1), gt (5 > -10 -> 1)
# Expected: No segments created, comparisons execute and push 1 values

# lt: -10 < 5 -> 1
push.i32 -10
push.i32 5
cmp.lt

# gt: 5 > -10 -> 1
push.i32 5
push.i32 -10
cmp.gt

halt
