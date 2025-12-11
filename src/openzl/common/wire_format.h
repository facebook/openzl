// Copyright (c) Meta Platforms, Inc. and affiliates.

/**
 * \file wire_format.h
 *
 * This is the place where the Zstrong frame format is centrally documented.
 * It also contains definitions which are common to both coder & decoder,
 * such as the Magic Number.
 *
 * The current wire format is not final.
 * It is most likely not optimal from a density perspective.
 * At target, the objective is to use a form of "dictionary compression"
 * to compress the frame header. However, this capability isn't present yet.
 */

/* Zstrong wire format, aka Frame Header:
 *
 * Frame Header :
 * - Zstrong Magic Number: 4 bytes (See Below)
 * - v21+: Frame property flags: 1 byte
 *   + bit0: checksum of decoded data
 *   + bit1: checksum of encoded data (also control frame header checksum)
 *   + bit2: presence of a comment field
 * - Input Type :
 *   + v13-: 0-byte , 1 Input assumed to be Serial
 *   + v14 : 1-byte, single Input, selectable type
 *   + v15-20:1-byte for 1-3 Inputs,
 *            2-bytes for 4-18 Inputs,
 *            3-bytes for 19-273 Inputs,
 *            5-bytes for 274-65809 Inputs (note: soft limit at 2048)
 *            + bitmap of Types (2-bits per Input for 6+ Inputs)
 *              There are BM=(((N-5)+3)/4) bytes used in this stream.
 *              Organized as a single large BM-bytes Little Endian number
 *              scanned from its lowest bits (shift >> 2 for each Input).
 *   + v21+: 1-byte for 0-14 Inputs
 *             + @note (cyan): is it useful to allow 0 input ?
 *             + type bitmap if nbInputs > 2
 *           2-bytes for 15-4110 Inputs (note: soft limit at 2048)
 *             + nbInputs = (B1 >> 4) + (B2 << 4)
 *             + type bitmap
 *          Type bitmap: 2-bits per Input
 *              Organized as a single large BM-bytes Little Endian number
 *              scanned from its lowest bits (shift >> 2 for each Input).
 *              Note: Format 1 stores the first 2 types in 1st byte.
 *  + v22+: VarInt format: Number of bytes of comment field
 *          Length x 1-byte: Arbitrary buffer of up to 10000 bytes (defined in
 *                           limits.h) containing a comment.
 *
 * Size of Inputs
 * v20-: NbInputs x LE_U32: decompressed size of each input, in bytes
 *   @note: can't represent huge content sizes >= 4 GB
 *   @note: Input size must necessarily be known upfront.
 * v21+:
 * - NbInputs x VarInt format: decompressed size +1 of each input, in bytes
 *   Value 0x00 means "unknown input size"
 * - NbInputStrings x VarInt: nb of Strings in the Input String of same rank.
 * - @note either all input sizes are known, or they are all unknown.
 *   A single 0x00 value is enough to state they are all unknown,
 *   whatever the nb and types of inputs.
 *
 * Header checksum
 * v21+ only: 1-byte checksum of input so far (XXH3_64bits & 255).
 *
 * -------------------------------
 * Block Header (once per Block):
 * Note: v20- features only one Block
 * Note: in v21+, a first byte 0 means "end of frame, no more block"
 *
 * - NbDecoders == nb of decoders + (version>=21): Varint (v9+) or 1-byte (v8-)
 * - NbSt == nb of stored streams : Varint (v9+) or 1-byte (v8-)
 *
 * - v20-: checksum properties (1 byte)
 *
 * - v21+: ***if*** Input Sizes are unknown:
 *      - NbInputs x ExtL248: size of each Inputs _at block level_,
 *                  which is necessarily known
 *      - NbInputStrings x ExtL248: nb of Strings in String Input of same rank.
 *
 * Decoding Map:
 * - For each decoder :
 *   + decoder type (standard, or custom)
 *   + ID of the decoder
 *   + DecHS : size of decoder's private header (in bytes)
 *   + nb of VOs (which are the inputs for the Decoders)
 *   + v16: nbRegen == nb of Regenerated Streams (outputs for Decoders)
 *     * assumed to be 1 for v15-
 *   + totalNbRegen x distance to regenerated stream IDs
 *
 * The Decoding Map is transposed, each field is compressed within its own lane:
 * - Array of Decoder Type is bitPacked (1-bit per flag)
 * - Array of Decoder ID is :
 *   + split into 2 streams, depending on being standard or custom
 *   + standard decoders ID are bitpacked
 *   + custom decoders IDs are varint-encoded
 * - Array of Private Header Sizes :
 *   + 0-sizes and non-zero sizes are identified by bit-packed flags
 *   + non-zero sizes are varint encoded
 * - V8+ : Array of nbVOs
 *   + 0-sizes and non-zero sizes are identified by bit-packed flags
 *   + non-zero sizes are shifted (-1) then varint encoded
 *     * v8 : up to 127 max
 * - V16+ : Array of nbRegens
 *   + bit-packed flags separate 1-regen decoders from 2+ ones
 *   + 2+ regens values are shifted (-2) then varint encoded
 * - Array of streamID distances is bitpacked,
 *   with nbBits depending on graph's size.
 *   There is 1 distance per regenerated stream.
 *   Since each Decoder regenerates at least 1 stream, nbRegens >= nbDecoders.
 *
 * Stored Streams Descriptions:
 * - For each Stored Stream : size of stored stream
 *   + varint-encoded
 *
 * Followed by the Stored Streams:
 * - Decoders' private header stream : size == sum of all TrHS
 * - Stream's content (concatenated back-to-back)
 *
 * Block Footer:
 * - Decompressed checksum : 4-bytes little endian,
 *       optional (see flag), low 32-bit of XXH3_64bit
 * - Compressed checksum : 4-bytes little endian,
 *       optional (see flag), low 32-bit of XXH3_64bit
 * @note a v20- frame contains only a single block
 * @note (@cyan)
 *
 * Frame Footer (v21+)
 * - End of Frame marker (1 byte, value 0)
 */

#ifndef ZSTRONG_COMMON_WIRE_FORMAT_H
#define ZSTRONG_COMMON_WIRE_FORMAT_H

#include "openzl/common/vector.h" // DECLARE_VECTOR_TYPE
#include "openzl/shared/portability.h"
#include "openzl/zl_errors.h"       // ZL_Report
#include "openzl/zl_opaque_types.h" // ZL_IDType

ZL_BEGIN_C_DECLS

/**
 * This magic number is selected because:
 * - It doesn't correspond to any known magic number so far
 * - It does not represent printable characters, neither ASCII nor UTF-8
 * - It reads as a large number (> 2 GB) in both little and big endian order
 *   hence is more likely to be detected invalid by codecs with no magic number
 *   starting directly with some 32-bit int size
 * - It's designed to be incremented, as updated wire formats are introduced
 */
#define ZSTRONG_MAGIC_NUMBER_BASE 0xD7B1A5C0

/**
 * Writes the 4-byte magic number for the given
 * format version number to the frame.
 *
 * @pre ZL_isFormatVersionSupported(version)
 * @pre dstCapacity >= 4
 */
void ZL_writeMagicNumber(void* dst, size_t dstCapacity, uint32_t version);

/**
 * Determines the format version from the magic number.
 *
 * @returns The format version of the magic number, or
 * an error if the magic number is bad, or if the format
 * version is not supported.
 */
ZL_Report ZL_getFormatVersionFromMagic(uint32_t magic);

/**
 * @returns if the version number is supported.
 */
bool ZL_isFormatVersionSupported(uint32_t version);

/**
 * Given a supported format version number, return the
 * magic number for that version.
 *
 * @pre ZL_isFormatVersionSupported(@version)
 * @version A supported format version number.
 * @returns The magic number for that format version.
 */
uint32_t ZL_getMagicNumber(uint32_t version);

#define CHUNK_HEADER_SIZE_MIN \
    (1 /*nbTransforms*/ + 1 /*nbStoredStream*/) // Just core elts

#define FRAME_HEADER_SIZE_MIN \
    (4 /*magic*/ + 4 /*dec.Size*/ + 1 /*eof marker*/) // Just core elts

/// Minimum wire format version required to support extra comment field
#define ZL_COMMENT_VERSION_MIN (22)

typedef struct {
    bool hasContentChecksum;
    bool hasCompressedChecksum;
    bool hasComment;
} ZL_FrameProperties;

typedef enum { trt_standard, trt_custom } TransformType_e;

typedef struct {
    TransformType_e trt;
    ZL_IDType trid;
} PublicTransformInfo;

DECLARE_VECTOR_TYPE(PublicTransformInfo)

/* ZL_StandardTransformID :
 * These IDs are used in the Zstrong Frame Header Format
 * to specify the decoder in charge of processing a set of inputs.
 * Note : These IDs **shall remain stable**, as much as possible,
 * modifying them makes Zstrong versioning support more difficult.
 **/
typedef enum {
    // note (@Cyan): 0 is a currently reserved value,
    // but maybe it doesn't have to be.
    ZL_StandardTransformID_delta_int = 1,
    ZL_StandardTransformID_transpose = 2,
    ZL_StandardTransformID_zigzag =
            3, /* note : might be removed in future if not useful */
    ZL_StandardTransformID_transpose_split            = 4,
    ZL_StandardTransformID_convert_serial_to_struct   = 5,
    ZL_StandardTransformID_convert_struct_to_serial   = 6,
    ZL_StandardTransformID_convert_struct_to_num_le   = 7,
    ZL_StandardTransformID_convert_num_to_struct_le   = 8,
    ZL_StandardTransformID_convert_serial_to_num_le   = 9,
    ZL_StandardTransformID_convert_num_to_serial_le   = 10,
    ZL_StandardTransformID_convert_serial_string      = 11,
    ZL_StandardTransformID_separate_string_components = 12,
    ZL_StandardTransformID_convert_struct_to_num_be   = 13,
    ZL_StandardTransformID_convert_serial_to_num_be   = 14,

    ZL_StandardTransformID_fse_deprecated           = 15,
    ZL_StandardTransformID_huffman_deprecated       = 16,
    ZL_StandardTransformID_huffman_fixed_deprecated = 17,
    // 18-19 : available
    ZL_StandardTransformID_rolz       = 20,
    ZL_StandardTransformID_fastlz     = 21,
    ZL_StandardTransformID_zstd       = 22,
    ZL_StandardTransformID_zstd_fixed = 23,
    ZL_StandardTransformID_field_lz   = 24,

    // TODO: Use local parameters to select quantization mode dynamically
    // instead of specialization for offsets / lengths.
    // Quantize for offsets with a power-of-2 scheme
    ZL_StandardTransformID_quantize_offsets = 25,
    // Quantize for lengths with a scheme that favors smaller lengths
    ZL_StandardTransformID_quantize_lengths = 26,

    ZL_StandardTransformID_bitpack_serial = 27,
    ZL_StandardTransformID_bitpack_int    = 28,
    ZL_StandardTransformID_flatpack       = 29,

    ZL_StandardTransformID_transpose_split2 = 30,
    ZL_StandardTransformID_transpose_split4 = 31,
    ZL_StandardTransformID_transpose_split8 = 32,

    ZL_StandardTransformID_float_deconstruct = 33,
    ZL_StandardTransformID_bitunpack         = 34,
    ZL_StandardTransformID_range_pack        = 35,

    ZL_StandardTransformID_tokenize_fixed   = 36,
    ZL_StandardTransformID_tokenize_numeric = 37,
    ZL_StandardTransformID_tokenize_string  = 38,

    ZL_StandardTransformID_splitn          = 40,
    ZL_StandardTransformID_splitByStruct   = 41,
    ZL_StandardTransformID_dispatchN_byTag = 42,

    ZL_StandardTransformID_merge_sorted = 43,

    ZL_StandardTransformID_constant_serial = 44,
    ZL_StandardTransformID_constant_fixed  = 45,
    ZL_StandardTransformID_prefix          = 46,

    ZL_StandardTransformID_splitn_struct = 47,
    ZL_StandardTransformID_splitn_num    = 48,

    ZL_StandardTransformID_fse_v2            = 49,
    ZL_StandardTransformID_huffman_v2        = 50,
    ZL_StandardTransformID_huffman_struct_v2 = 51,

    ZL_StandardTransformID_fse_ncount = 52,

    ZL_StandardTransformID_divide_by = 53,

    ZL_StandardTransformID_dispatch_string = 54,

    ZL_StandardTransformID_concat_serial = 55,

    ZL_StandardTransformID_dedup_num = 56,

    ZL_StandardTransformID_concat_num    = 57,
    ZL_StandardTransformID_concat_struct = 58,
    ZL_StandardTransformID_concat_string = 59,

    ZL_StandardTransformID_parse_int = 60,

    ZL_StandardTransformID_interleave_string = 61,

    ZL_StandardTransformID_lz4 = 62,

    ZL_StandardTransformID_end =
            63 // last id, used to detect end of ID range (impacts
               // header encoding) give some room to be able to add new
               // transforms without breaking encoder / decoder
} ZL_StandardTransformID;

// Min version of standard transforms is published
// for standard transforms which can be dynamically defined at runtime.
typedef enum {
    ZL_StandardTransformMinVersion_splitByStruct = 9,
} ZL_StandardTransformMinVersion;

ZL_END_C_DECLS

#endif // ZSTRONG_COMMON_WIRE_FORMAT_H
