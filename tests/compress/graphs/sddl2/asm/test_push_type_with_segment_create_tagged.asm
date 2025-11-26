# Test: End-to-end typed segment creation pipeline
# Input: 12
# Tests the full workflow: push tag, push type, push count, create tagged segment
# Creates a tagged segment with tag=100, type=I32LE, element_count=3
# Expected: 1 segment created with tag=100, type=I32LE, width=1, size=12 bytes (3 elements * 4 bytes)

push.tag 100
push.type.i32le
push.u32 3
segment.create_tagged
halt
