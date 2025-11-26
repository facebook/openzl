# Test using push.current_pos for arithmetic operations
# Input: 20
# Demonstrates using cursor position for dynamic segment sizing
# Uses tagged segments to prevent merging
push.tag 100
push.type.bytes
push.i32 10
segment.create_tagged    ; Create 10-byte segment, cursor → 10
push.tag 200             ; Stack: [tag:200]
push.type.bytes          ; Stack: [tag:200, type:BYTES]
push.current_pos         ; Stack: [tag:200, type:BYTES, 10]
push.i32 0               ; Stack: [tag:200, type:BYTES, 10, 0]
math.sub                 ; Stack: [tag:200, type:BYTES, 10]
segment.create_tagged    ; Create segment with calculated size (10 - 0 = 10)
halt
