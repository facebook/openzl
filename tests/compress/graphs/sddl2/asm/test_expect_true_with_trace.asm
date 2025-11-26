# Test: expect_true with rich trace output
# Expected: SDDL2_VALIDATION_FAILED
# Verifies that trace.start + operations + expect_true failure shows rich traces

trace.start         # Start trace collection
push.i32 150
push.i32 200
cmp.eq              # Should record: "cmp.eq: 150 == 200 → 0"
push.i32 5
push.i32 10
cmp.lt              # Should record: "cmp.lt: 5 < 10 → 1"
logic.or            # Should record: "logic.or: 0 || 1 → 1"
logic.not           # Should record: "logic.not: !1 → 0"
expect_true         # Should fail and dump trace showing all 4 operations
halt
