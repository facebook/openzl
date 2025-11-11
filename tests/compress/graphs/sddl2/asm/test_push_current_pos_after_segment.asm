# Test push.current_pos after creating a segment
# Creates a 5-byte segment, then checks cursor position
# Uses tagged segments to prevent merging
push.tag 100
push.type.bytes
push.i32 5
segment.create_tagged
push.tag 200
push.type.bytes
push.current_pos    ; Should push 5 (cursor advanced by previous segment)
segment.create_tagged  ; Create segment from position 5 with different tag
halt
