# Test: Push various type values onto the stack
# Tests that push.type opcodes execute successfully without errors
# Pushes U8, I32LE, and F64BE type values, then halts
# Expected: No segments created, program completes successfully

push.type.u8
push.type.i32le
push.type.f64be
halt
