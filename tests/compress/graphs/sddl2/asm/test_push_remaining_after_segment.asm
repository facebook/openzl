# Test push.remaining after creating a segment
# Input: 10
# Input buffer size: 10 bytes
# After creating a 5-byte segment, 5 bytes should remain

push.i32 5          ; Size for segment
segment.create_unspecified

push.remaining      ; Should push 5 (10 - 5 = 5 bytes remaining)
segment.create_unspecified  ; Create segment with the remaining 5 bytes
halt
