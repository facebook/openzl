# Test: Create multiple typed segments with different types
# Input: 5
# Tests creating two consecutive segments with different types
# Segment 1: tag=100, type=U8, element_count=1 (1 byte)
# Segment 2: tag=200, type=F32LE, element_count=1 (4 bytes)
# Expected: 2 segments created with different types and different tags
# (same tag cannot be used with different types - semantic constraint)

# Segment 1: tag=100, type=U8, size=1
push.tag 100
push.type.u8
push.u32 1
segment.create_tagged

# Segment 2: tag=200, type=F32LE, size=1
push.tag 200
push.type.f32le
push.u32 1
segment.create_tagged

halt
