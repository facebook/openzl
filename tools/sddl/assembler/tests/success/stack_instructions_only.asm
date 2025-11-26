# Test: stack instructions without values (would underflow at runtime)
# The assembler should accept this - VM detects underflow
stack.dup
stack.drop
stack.swap
stack.over
stack.rot
halt
