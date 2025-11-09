# Test: stack.dup - duplicate top of stack
# Stack: ... a → ... a a
push.u32 42
stack.dup
halt
