# Test: expect_true failure with non-empty stack
# Expected: SDDL2_VALIDATION_FAILED
# Demonstrates trace output with remaining stack values

trace.start
push.i32 100        # This will remain on stack
push.i32 200        # This will remain on stack
push.i32 5
push.i32 10
cmp.lt              # 5 < 10 → 1
push.i32 0          # Force to false
logic.and           # 1 && 0 → 0
expect_true         # Fail and show trace + stack (should show 100, 200)
halt
