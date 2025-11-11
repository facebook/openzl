# Test creating a zero-size segment explicitly
# This is a minimal test to check if zero-size segments work

push.i32 0
segment.create_unspecified
halt
