# Test push.remaining combined with push.current_pos
# Verify that current_pos + remaining = buffer_size (10 bytes)
# Input buffer size: 10 bytes

push.i32 3          ; Create a 3-byte segment
segment.create_unspecified

push.current_pos    ; Should be 3
push.remaining      ; Should be 7

math.add            ; Should be 10 (3 + 7 = buffer size)
segment.create_unspecified  ; Create segment with size 10
halt
