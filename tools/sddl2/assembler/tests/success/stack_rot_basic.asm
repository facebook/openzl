# Test: stack.rot - rotate top three left
# Stack: ... a b c → ... b c a
push.u32 10
push.u32 20
push.u32 30
stack.rot
halt
