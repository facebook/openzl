# Test that push.current_pos doesn't advance the cursor (read-only operation)
# Input: 5
# Calls push.current_pos twice and verifies they return the same value (diff = 0)
push.i32 2
segment.create_unspecified
push.current_pos
push.current_pos
math.sub                    ; Should be 0 (same position)
segment.create_unspecified  ; Creates zero-size segment (will merge with first)
halt
