# Test: stack.rot - three rotations return to original
# 3 rotations: abc → bca → cab → abc
push.u32 1
push.u32 2
push.u32 3
stack.rot
stack.rot
stack.rot
halt
