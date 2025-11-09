# Test: Comparison operations with false results
# Tests that comparisons correctly return 0 for false conditions
# Tests: eq (10 == 5 -> 0), lt (10 < 5 -> 0)
# Expected: No segments created, comparisons execute and push 0 values

# eq: 10 == 5 -> 0
push.i32 10
push.i32 5
cmp.eq

# lt: 10 < 5 -> 0
push.i32 10
push.i32 5
cmp.lt

halt
