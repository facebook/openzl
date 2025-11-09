# Test: STACK operations with mixed value types
# Tests that stack operations work correctly with different value types
# Uses I64, Tag, and Type values to test type-agnostic stack operations
# Stack sequence: push I64 -> push Tag -> push Type -> dup Type -> swap -> drop all
# Expected: No segments created, all stack operations execute successfully

push.i32 10
push.tag 100
push.type.u8
stack.dup
stack.swap
stack.drop
stack.drop
stack.drop
stack.drop
halt
