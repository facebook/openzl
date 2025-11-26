# Test that push.remaining doesn't advance the cursor (read-only operation)
# Input: 10
# Verifies that calling push.remaining twice returns the same value
# Input buffer size: 10 bytes

push.i32 3
segment.create_unspecified  ; Consume 3 bytes, 7 remaining

push.remaining              ; Should be 7
push.remaining              ; Should still be 7 (no side effects)
cmp.eq                      ; Compare: 7 == 7? Should be 1 (true)

# Use the comparison result (1) to create a 1-byte segment
segment.create_unspecified
halt
