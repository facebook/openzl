# EXPECT-ERROR: Unknown instruction or unexpected token
push.u32 1
push.u32 2
push.u32 3
stack.rot 3
halt
