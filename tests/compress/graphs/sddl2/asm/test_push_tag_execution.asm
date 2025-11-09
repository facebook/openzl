# Test: Push tag value onto the stack
# Tests that push.tag opcode executes successfully without errors
# Pushes tag value 100 onto the stack, then halts
# Expected: No segments created, program completes successfully

push.tag 100
halt
