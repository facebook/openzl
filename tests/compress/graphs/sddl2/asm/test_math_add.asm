# Interpreter Test: Basic math.add operation
# Tests: 10 + 5 = 15
# Expected result: Stack contains single I64 value (15)

push.i32 10
push.i32 5
math.add
halt
