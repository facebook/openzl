# Test push.remaining at beginning of buffer
# Input buffer size: 10 bytes
push.remaining              ; Should push 10 (full buffer available)
segment.create_unspecified  ; Create segment with size 10
halt
