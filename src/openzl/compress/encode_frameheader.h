// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef OPENZL_COMPRESS_ENCODE_FRAMEHEADER_H
#define OPENZL_COMPRESS_ENCODE_FRAMEHEADER_H

#include "openzl/common/wire_format.h" // ZL_FrameHeaderInfo
#include "openzl/shared/portability.h"
#include "openzl/zl_buffer.h" // ZL_RBuffer
#include "openzl/zl_common_types.h"
#include "openzl/zl_data.h"   // ZL_Type
#include "openzl/zl_errors.h" // ZL_Report

ZL_BEGIN_C_DECLS

/**
 * @brief Descriptor for an input stream's properties.
 *
 * This structure describes the characteristics of an input stream used in
 * compression, including its size and data type. It's used to track and
 * validate input streams during compression session setup and metadata
 * generation.
 */
typedef struct {
    ZL_Type type;    /**< The data type classification of the stream */
    size_t byteSize; /**< The size of the input stream in bytes */
    size_t numElts;  /**< Number of elts in the Input */
} InputDesc;

typedef struct {
    const ZL_FrameProperties* fprop;
    const InputDesc* inputDescs; /**< Array of input stream's properties */
    size_t numInputs;
    ZL_Comment comment;
} EFH_FrameInfo;

/**
 * Writes the frame header into the destination buffer for the given format
 * version.
 *
 * @returns The size of the frame header on success, or an error code.
 */
ZL_Report EFH_writeFrameHeader(
        void* dst,
        size_t dstCapacity,
        const EFH_FrameInfo* fip,
        uint32_t version);

/**
 * @brief Comprehensive metadata structure describing a completed compression
 * session.
 *
 * This structure contains all the metadata and buffer information needed to
 * create a compressed frame. It provides detailed information about the
 * compression pipeline execution, including transform usage, input/output
 * relationships, and data buffers. The structure contains pointers into the
 * compression context's internal arrays, so it must not outlive the context
 * that generated it.
 */
typedef struct {
    const InputDesc* inputDescs; /**< Input stream's description */
    size_t nbSessionInputs;      /**< Number of input streams processed */
    size_t nbTransforms; /**< Number of transforms executed in the pipeline */
    const PublicTransformInfo* trInfo; /**< Array of transform metadata for each
                                          executed transform */
    const size_t* trHSizes;   /**< Array of transform header sizes (bytes) */
    const size_t* nbVOs;      /**< Array of output counts per transform */
    const size_t* nbTrInputs; /**< Array of input counts per transform */
    const uint32_t*
            distances;  /**< Array of dependency distances between transforms */
    size_t nbDistances; /**< Number of dependency distance entries */
    const ZL_RBuffer* storedBuffs; /**< Array of data buffers to be stored in
                                      compressed output */
    size_t nbStoredBuffs;          /**< Number of data buffers to store */
} GraphInfo;

/**
 * Writes the Chunk header into the destination buffer for the given format
 * version.
 *
 * @returns The size of the Chunk header on success, or an error code.
 */
ZL_Report EFH_writeChunkHeader(
        void* dst,
        size_t dstCapacity,
        const ZL_FrameProperties* info,
        const GraphInfo* gip,
        uint32_t version);

/* @note (@cyan) is it really useful to expose this struct ? */
typedef struct EFH_Interface_s {
    /**
     * Writes the frame header into the frame.
     *
     * @returns The size of the frame header on success, or an error code.
     */
    ZL_Report (*writeFrameHeader)(
            const struct EFH_Interface_s*,
            void*,
            size_t,
            const EFH_FrameInfo* fip);

    ZL_Report (*writeChunkHeader)(
            const struct EFH_Interface_s*,
            void*,
            size_t,
            const ZL_FrameProperties*,
            const GraphInfo*);

    uint32_t formatVersion;
} EFH_Interface;

/**
 * Returns the encoder used to encode the given format version.
 * The format version must be valid and supported.
 *
 * @pre ZL_isFormatVersionSupported(formatVersion).
 * @returns The encoder interface used to encode the given format version.
 */
EFH_Interface EFH_getFrameHeaderEncoder(uint32_t formatVersion);

ZL_END_C_DECLS

#endif // OPENZL_COMPRESS_ENCODE_FRAMEHEADER_H
