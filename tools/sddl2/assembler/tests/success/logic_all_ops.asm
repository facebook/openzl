; Test: all logical operations together
; Expected: compiles successfully

; Test AND
push.i32 0xFF00
push.i32 0x0FF0
logic.and
stack.drop

; Test OR
push.i32 0x00F0
push.i32 0x0F00
logic.or
stack.drop

; Test XOR
push.i32 0xAAAA
push.i32 0x5555
logic.xor
stack.drop

; Test NOT
push.i32 0x0F0F
logic.not
stack.drop

halt
