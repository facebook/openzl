# Test push.current_pos with multiple segments
# Input: 16
# Verifies cursor position advances correctly through multiple operations
# Uses tagged segments to prevent merging
push.tag 100
push.type.bytes
push.i32 3
segment.create_tagged       ; Segment from 0-3
push.tag 200
push.type.bytes
push.current_pos            ; Should be 3
push.i32 2
math.add                    ; 3 + 2 = 5
segment.create_tagged       ; Segment from 3-8 (size 5)
push.tag 300
push.type.bytes
push.current_pos            ; Should be 8
segment.create_tagged       ; Remaining segment from 8-end
halt
