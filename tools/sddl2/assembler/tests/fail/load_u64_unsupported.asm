# EXPECT-ERROR: Unknown instruction
# Test: load.u64le - should fail (only i64 supported for 64-bit, not u64)
push.i32 0
load.u64le
halt
