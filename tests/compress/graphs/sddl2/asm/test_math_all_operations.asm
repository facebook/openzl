# Test: All 7 MATH operations
# Tests that all math operation opcodes dispatch correctly
# Operations tested: add, sub, mul, div, mod, abs, neg
# Note: Stack accumulates intermediate results (no cleanup)
# Expected: No segments created, all operations execute successfully

# add: 10 + 5 = 15
push.i32 10
push.i32 5
math.add

# sub: 20 - 8 = 12
push.i32 20
push.i32 8
math.sub

# mul: 3 * 4 = 12
push.i32 3
push.i32 4
math.mul

# div: 20 / 4 = 5
push.i32 20
push.i32 4
math.div

# mod: 17 % 5 = 2
push.i32 17
push.i32 5
math.mod

# abs: abs(-42) = 42
push.i32 -42
math.abs

# neg: neg(10) = -10
push.i32 10
math.neg

halt
