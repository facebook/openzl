# Test push.remaining with multiple segments
# Input: 15
# Input buffer size: 10 bytes
# Track remaining bytes after each segment creation

push.i32 3          ; First segment: 3 bytes
segment.create_unspecified

push.remaining      ; Should be 7 (10 - 3 = 7)

push.i32 5          ; Second segment: 5 bytes
segment.create_unspecified

push.remaining      ; Should be 2 (7 - 5 = 2)

# Verify the two remaining values
math.sub            ; Should be 5 (7 - 2 = 5)
segment.create_unspecified  ; Create segment with size 5
halt
