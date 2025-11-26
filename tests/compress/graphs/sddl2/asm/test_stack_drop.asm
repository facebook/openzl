# Interpreter Test: Stack drop operation
# Tests: Push two values, drop top value
# Expected result: Stack contains one I64 value (10)

push.i32 10
push.i32 20
stack.drop
halt
