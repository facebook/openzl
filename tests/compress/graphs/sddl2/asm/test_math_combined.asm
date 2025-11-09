# Interpreter Test: Combined math operations
# Tests: (2 + 3) * 4 = 20
# Expected result: Stack contains single I64 value (20)

push.i32 2
push.i32 3
math.add
push.i32 4
math.mul
halt
