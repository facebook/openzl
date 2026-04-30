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
# SECTION 2: Build Conditional Type Structure
# ------------------------------------------

# Optional XNO (Float32LE if STNUM > 0)
push.type.f32le
push.u32 12
load.i32le              # STNUM
push.zero
cmp.le                  # STNUM <= 0 → drops when STNUM <= 0 → keeps when STNUM > 0
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
# SECTION 3: Count Types and Create Structure
# Stack: Type0, Type1, ..., TypeN
# ------------------------------------------

push.stack_depth
type.structure

# ------------------------------------------
# SECTION 4: Validate entry_size using type.sizeof
# Stack: StarEntry_type
# ------------------------------------------

stack.dup               # Duplicate the type for validation
type.sizeof             # Get size of the structure type
push.u32 24
load.i32le              # NBENT (expected entry size)
cmp.eq
expect_true

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
