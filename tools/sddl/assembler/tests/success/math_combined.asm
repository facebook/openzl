# Test: combination of math operations
# Example: (10 + 5) * 3 - 2
push.i32 10
push.i32 5
math.add      # 15
push.i32 3
math.mul      # 45
push.i32 2
math.sub      # 43
halt
