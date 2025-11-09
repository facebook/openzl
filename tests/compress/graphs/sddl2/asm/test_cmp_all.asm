# Interpreter Test: Comparison operations (all 6 operations)
# Tests: eq, ne, lt, le, gt, ge
# Expected result: Stack contains 6 I64 values (all should be 1 for true)

# eq: 10 == 10 -> 1
push.i32 10
push.i32 10
cmp.eq

# ne: 10 != 5 -> 1
push.i32 10
push.i32 5
cmp.ne

# lt: 5 < 10 -> 1
push.i32 5
push.i32 10
cmp.lt

# le: 10 <= 10 -> 1
push.i32 10
push.i32 10
cmp.le

# gt: 10 > 5 -> 1
push.i32 10
push.i32 5
cmp.gt

# ge: 10 >= 10 -> 1
push.i32 10
push.i32 10
cmp.ge

halt
