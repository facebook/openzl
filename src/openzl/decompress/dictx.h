// Copyright (c) Meta Platforms, Inc. and affiliates.

// ZL_Decoder
// is a state for a Decompression Interface stage(D.I.Ctx)
//
// Note : however, after usage, it also kind of looks like dict.x,
//        which might be confusing for people used to `dict`,
//        so maybe a different name could be considered...

#ifndef ZSTRONG_DICTX_H
#define ZSTRONG_DICTX_H

#include "openzl/decompress/dtransforms.h" // DTransform
#include "openzl/shared/portability.h"
#include "openzl/zl_dtransform.h"   // ZL_Decoder
#include "openzl/zl_opaque_types.h" // ZL_IDType

ZL_BEGIN_C_DECLS

#if 0
/* ==============================================================
 * For reference : Public symbols, declared in zs2_dtransform.h
 * ============================================================== */

ZL_Data*
ZL_Decoder_create1OutStream(ZL_Decoder* dictx, size_t eltsCapacity, size_t eltWidth);

ZL_RBuffer ZL_Decoder_getCodecHeader(const ZL_Decoder* dictx);

#endif

/* ==============================================================
 * The following symbols are considered _private_,
 * they are not meant to be used by a user's custom decoder
 * but are useful for either the graph engine
 * or for zstrong's provided "standard" transforms.
 * ==============================================================
 */

// state structure definition, useful for decompress2.c
// where a Decompression Interface Context (DICtx)
// is created and initialized in place, on the stack
struct ZL_Decoder_s {
    ZL_DCtx* dctx;
    const DTransform* dt;
    void** statePtr;
    Arena* workspaceArena;
    const ZL_IDType* regensID;
    size_t nbRegens;
    ZL_RBuffer thContent;
    const void* ddict; // materialized dict object (NULL if none)
}; // typedef'd to ZL_Decoder within "zs2_dtransform.h"

ZL_Decoder* DI_createDICtx(ZL_DCtx* dctx);
void DI_freeDICtx(ZL_Decoder* dictx);

/* DI_reference1OutStream() :
 * Only valid for single-regen decoder Transforms.
 * Create the output stream
 * as a read-only reference into an existing stream @ref
 * starting at byte position @offsetBytes.
 * The use of bytes for the offset position is justified by the fact
 * that this function is used notably for conversion operations, where
 * the Types of the referenced and destination streams are different.
 * Streams created this way don't need to be committed, since they can no
 * longer be edited after their creation.
 */
ZL_Output* DI_reference1OutStream(
        ZL_Decoder* dictx,
        const ZL_Input* ref,
        size_t offsetBytes,
        size_t eltWidth,
        size_t nbElts);

/* DI_outStream_asReference() :
 * More general version, can be used for MI Transforms.
 */
ZL_Output* DI_outStream_asReference(
        ZL_Decoder* dictx,
        int index,
        ZL_Input const* ref,
        size_t offsetBytes,
        size_t eltWidth,
        size_t nbElts);

/**
 * @returns The ZStrong format version of the frame which is currently
 * being decompressed.
 */
unsigned DI_getFrameFormatVersion(const ZL_Decoder* dictx);

/*
 * @returns The number of streams to regenerate for the current transform.
 */
size_t DI_getNbRegens(const ZL_Decoder* dictx);

ZL_END_C_DECLS

#endif // ZSTRONG_DECOMPRESS_DCTX_H
