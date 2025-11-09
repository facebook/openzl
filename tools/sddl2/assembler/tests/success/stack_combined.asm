# Test: combination of stack operations
push.u32 1
push.u32 2
push.u32 3
stack.swap     # 1 3 2
stack.over     # 1 3 2 3
stack.rot      # 1 2 3 3
stack.dup      # 1 2 3 3 3
stack.drop     # 1 2 3 3
halt
