// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_STREAM_DUMP2_H
#define ZSTRONG_STREAM_DUMP2_H

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct ZL_DCtx_s ZL_DCtx;

// You must provide an implementation of this function to register any custom
// decoders needed to understand the frame. A no-op implementation is provided
// in stream_dump2_noop_shim.c, for applications that don't require any custom
// transforms.
void stream_dump_register_decoders(ZL_DCtx* dctx);

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_STREAM_DUMP2_H
