# Test push.current_pos at beginning of buffer
# Expects: cursor at position 0
push.current_pos    ; Should push 0
segment.create_unspecified
halt
