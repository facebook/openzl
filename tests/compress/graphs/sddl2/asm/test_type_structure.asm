; Test type.structure opcode
; Creates a structure {U8, I16LE, I32LE} and verifies it works

push.type.u8
push.type.i16le
push.type.i32le
push.i64 3
type.structure

halt
