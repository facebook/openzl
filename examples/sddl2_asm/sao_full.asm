############################################################
# SAO Star Catalog Format (B1950 / J2000)
# SDDL2 Assembly - Full Implementation
#
# Translation of sao_full.sddl to SDDL2 assembly using only
# existing opcodes (no var.* or macros needed).
#
# Strategy: Reload header values from buffer as needed, use
# push.stack_depth for counting conditional types.
############################################################

# ------------------------------------------
# SECTION 1: Parse Header (28 bytes)
# ------------------------------------------
push.i32 28
segment.create_unspecified

# ------------------------------------------
# SECTION 2: Compute and Validate entry_size
# ------------------------------------------
# entry_size = 18 (base) + conditional fields
# Base: SRA0(8) + SDEC0(8) + ISP(2) = 18

push.u32 18

# Add 4 if STNUM >= 0 (XNO field)
push.u32 12
load.i32le              # STNUM
push.zero
cmp.ge
push.u32 4
math.mul
math.add

# Add 2 * abs(NMAG) for MAG array
push.u32 20
load.i32le              # NMAG
math.abs
push.u32 2
math.mul
math.add

# Add 8 if MPROP >= 1 (XRPM + XDPM)
push.u32 16
load.i32le              # MPROP
push.u32 1
cmp.ge
push.u32 8
math.mul
math.add

# Add 8 if MPROP == 2 (SVEL)
push.u32 16
load.i32le              # MPROP
push.u32 2
cmp.eq
push.u32 8
math.mul
math.add

# Add abs(STNUM) if STNUM < 0 (NAME)
push.u32 12
load.i32le              # STNUM
stack.dup
push.zero
cmp.lt
stack.swap
math.abs
math.mul
math.add

# Validate entry_size == NBENT
push.u32 24
load.i32le              # NBENT
cmp.eq
expect_true

# ------------------------------------------
# SECTION 3: Build Conditional Type Structure
# ------------------------------------------

# Optional XNO (Float32LE if STNUM >= 0)
push.type.f32le
push.u32 12
load.i32le              # STNUM
push.zero
cmp.lt
stack.drop_if

# Always: SRA0 (Float64LE)
push.type.f64le

# Always: SDEC0 (Float64LE)
push.type.f64le

# Always: ISP (Bytes[2])
push.type.bytes
push.u32 2
type.fixed_array

# Optional MAG (Int16LE[abs(NMAG)] if NMAG != 0)
push.u32 20
load.i32le              # NMAG
stack.dup
push.zero
cmp.eq
stack.swap
math.abs
stack.dup
push.u32 10
cmp.le
expect_true
push.type.i16le
stack.swap
type.fixed_array
stack.swap
stack.drop_if

# Optional XRPM (Float32LE if MPROP >= 1)
push.type.f32le
push.u32 16
load.i32le              # MPROP
push.u32 1
cmp.lt
stack.drop_if

# Optional XDPM (Float32LE if MPROP >= 1)
push.type.f32le
push.u32 16
load.i32le              # MPROP
push.u32 1
cmp.lt
stack.drop_if

# Optional SVEL (Float64LE if MPROP == 2)
push.type.f64le
push.u32 16
load.i32le              # MPROP
push.u32 2
cmp.ne
stack.drop_if

# Optional NAME (Bytes[abs(STNUM)] if STNUM < 0)
push.u32 12
load.i32le              # STNUM
stack.dup
push.zero
cmp.ge
stack.swap
math.abs
push.type.bytes
stack.swap
type.fixed_array
stack.swap
stack.drop_if

# ------------------------------------------
# SECTION 4: Count Types and Create Structure
# Stack: Type0, Type1, ..., TypeN
# ------------------------------------------

push.stack_depth
type.structure

# ------------------------------------------
# SECTION 5: Create Stars Segment
# Stack: StarEntry_type
# ------------------------------------------
push.tag 1
stack.swap

push.u32 8
load.i32le              # STARN
math.abs

segment.create_tagged

halt
