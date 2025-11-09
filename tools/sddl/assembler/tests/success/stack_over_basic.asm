# Test: stack.over - copy second to top
# Stack: ... a b → ... a b a
push.u32 10
push.u32 20
stack.over
halt
