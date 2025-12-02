// Copyright (c) Meta Platforms, Inc. and affiliates.

#ifndef ZSTRONG_ZS2_OPAQUE_TYPES_H
#define ZSTRONG_ZS2_OPAQUE_TYPES_H

#include <stddef.h> // size_t

#if defined(__cplusplus)
extern "C" {
#endif

// This is a list of opaque types
// employed by Zstrong's Public APIs.
// Their declaration is required for compilers.
// However, users should _never_ access private members of these opaque types.
// This is extremely important, there is no stability guarantee.
// Moreover, the main reason for these opaque types to exist is
// to prevent confusion between different opaque types
// and erroneous manipulation of values.

typedef unsigned int ZL_IDType;

// opaque => never use definition !!!
typedef struct {
    ZL_IDType sid;
} ZL_DataID;

typedef struct {
    ZL_IDType nid;
} ZL_NodeID;

typedef struct {
    ZL_IDType gid;
} ZL_GraphID;

// Helper macros for creating ZL_NodeID and ZL_GraphID in a C++ compatible way
#if defined(__cplusplus)
// C++ compatible versions using constructor syntax
#    define ZL_MAKE_NODE_ID(id) (ZL_NodeID{ (id) })
#    define ZL_MAKE_GRAPH_ID(id) (ZL_GraphID{ (id) })
#else
// C99 compound literals
#    define ZL_MAKE_NODE_ID(id) ((ZL_NodeID){ .nid = (id) })
#    define ZL_MAKE_GRAPH_ID(id) ((ZL_GraphID){ .gid = (id) })
#endif

// Incomplete types
typedef struct Stream_s Stream;
typedef Stream ZL_Data;
typedef struct ZL_Input_s ZL_Input;
typedef struct ZL_Output_s ZL_Output;
typedef ZL_Input ZL_TypedRef;
typedef struct ZL_Compressor_s ZL_Compressor;
typedef struct ZL_CompressorSerializer_s ZL_CompressorSerializer;
typedef struct ZL_CompressorDeserializer_s ZL_CompressorDeserializer;
typedef struct ZL_CCtx_s ZL_CCtx;
typedef struct ZL_DCtx_s ZL_DCtx;
typedef struct ZL_Encoder_s ZL_Encoder;
typedef struct ZL_Decoder_s ZL_Decoder;
typedef struct ZL_Selector_s ZL_Selector;
typedef struct ZL_Graph_s ZL_Graph;
typedef struct ZL_Edge_s ZL_Edge;
typedef struct ZL_Segmenter_s ZL_Segmenter;

// Generic List construction macro (C99)
#define ZL_LIST_SIZE(_type, ...) \
    (sizeof((const _type[]){ __VA_ARGS__ }) / sizeof(_type))

#define ZL_GENERIC_LIST(_type, ...) \
    (const _type[]){ __VA_ARGS__ }, ZL_LIST_SIZE(_type, __VA_ARGS__)

#if defined(__cplusplus)
} // extern "C"
#endif

#endif // ZSTRONG_ZS2_OPAQUE_TYPES_H
