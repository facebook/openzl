# Test: All load operations - assembler test
# Verify all load instructions assemble correctly
# 8-bit loads
push.i32 0
load.u8
push.i32 1
load.i8
# 16-bit LE loads
push.i32 0
load.u16le
push.i32 2
load.i16le
# 32-bit LE loads
push.i32 0
load.u32le
push.i32 4
load.i32le
# 64-bit LE loads
push.i32 0
load.i64le
# 16-bit BE loads
push.i32 0
load.u16be
push.i32 2
load.i16be
# 32-bit BE loads
push.i32 0
load.u32be
push.i32 4
load.i32be
# 64-bit BE loads
push.i32 0
load.i64be
halt
