# ==========================================
# SAO Star Catalog - SDDL2 Assembly
# Compiled from sao_silesia.sddl
# ==========================================
#
# This demonstrates the power of push.remaining for dynamic
# data layout where the number of records isn't known upfront.
#
# File structure:
#   - 28-byte header
#   - Variable number of 28-byte StarEntry records
#
# Each StarEntry contains:
#   - SRA0:  Float64LE (8 bytes) - Right Ascension
#   - SDEC0: Float64LE (8 bytes) - Declination
#   - ISP:   Bytes[2]   (2 bytes) - Spectral type (ASCII)
#   - MAG:   Int16LE    (2 bytes) - Magnitude
#   - XRPM:  Float32LE  (4 bytes) - RA proper motion
#   - XDPM:  Float32LE  (4 bytes) - Dec proper motion
#   Total: 28 bytes per star
# ==========================================

# ------------------------------------------
# Step 1: Process header
# ------------------------------------------
push.i32 28             # Header size
segment.create_unspecified

# ------------------------------------------
# Step 2: Build StarEntry structure type
# ------------------------------------------
push.type.f64le         # SRA0: Right Ascension (8 bytes) - FIRST
push.type.f64le         # SDEC0: Declination (8 bytes)

# ISP: Spectral type - 2 bytes as Bytes[2]

#push.type.bytes         # Base type: Bytes
#push.i32 2              # Array size: 2
#type.fixed_array        # Creates Bytes[2] type

push.type.u16le         # Treat 2 bytes as a 16-bit unsigned integer
                        # This is not completely accurate,
                        # but the trainer works better this way

push.type.i16le         # MAG: Magnitude (2 bytes)
push.type.f32le         # XRPM: RA proper motion (4 bytes)
push.type.f32le         # XDPM: Dec proper motion (4 bytes) - LAST

# Now create the structure with all 6 members
push.i32 6              # Member count
type.structure          # Stack: StarEntry_type

# ------------------------------------------
# Step 4: Create stars segment (tag 2)
# ------------------------------------------
# Stack currently has: StarEntry_type
# We need: tag, type, count for segment.create_tagged

push.tag 2              # Tag for "stars"
stack.swap              # Stack: tag, StarEntry_type

# ------------------------------------------
# Step 3: Calculate number of star entries
# ------------------------------------------
# After consuming 28 bytes for header, use remaining bytes
# to determine how many complete StarEntry records exist.
# Each StarEntry is exactly 28 bytes.

push.remaining          # Stack: remaining_bytes
push.u32 28             # Stack: remaining_bytes, 28
math.div                # Stack: star_count = remaining_bytes / 28

# Note: In production, you'd want to validate that
# remaining_bytes % 28 == 0 to ensure complete records.
# For this example, we assume the file is well-formed.

# ------------------------------------------
# Step 4: Create Segment
# ------------------------------------------
# Stack: tag, StarEntry_type, star_count
segment.create_tagged

halt    # note: optional
