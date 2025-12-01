# EXPECT-ERROR: Unknown instruction
# Test: load.f32le - should fail (floating point loads not supported)
push.i32 0
load.f32le
halt
