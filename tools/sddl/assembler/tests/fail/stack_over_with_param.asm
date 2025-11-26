# EXPECT-ERROR: Unknown instruction or unexpected token
push.u32 1
push.u32 2
stack.over 2
halt
