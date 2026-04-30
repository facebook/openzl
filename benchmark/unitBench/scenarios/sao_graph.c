// Copyright (c) Meta Platforms, Inc. and affiliates.

/*
 *  List transforms to be benchmarked
 *  Part of zstrong project
 */

/* ===   Dependencies   === */
#include "benchmark/unitBench/scenarios/sao_graph.h"

#include <stdlib.h> // abort

#include "openzl/codecs/zl_field_lz.h" // ZL_GRAPH_FIELD_LZ
#include "openzl/zl_compressor.h"      // ZL_Compressor_setParameter
#include "openzl/zl_data.h"            // ZL_Type_struct

/* ==================================================
 * Graph for SAO
 * ==================================================
 */

// Goal of this graph :
// stronger compression ratio than cmix on sao (3726989)
// at fastest compression speed possible
ZL_GraphID sao_graph_v1(ZL_Compressor* cgraph)
{
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION))) {
        abort();
    }
    if (ZL_isError(ZL_Compressor_setParameter(
                cgraph, ZL_CParam_compressionLevel, 1))) {
        abort();
    }
    /* The SAO format consists of a header,
     * which is 28 bytes for the dirSilesia/sao sample specifically,
     * followed by an array of structures, each one describing a star.
     *
     * For the record, here is the Header format (it's currently ignored):
     *
     * Integer*4 STAR0=0   Subtract from star number to get sequence number
     * Integer*4 STAR1=1   First star number in file
     * Integer*4 STARN=258996  Number of stars in file (pos 8)
     * Integer*4 STNUM=1   0 if no star i.d. numbers are present
     *                     1 if star i.d. numbers are in catalog file
     *                     2 if star i.d. numbers are  in file
     * Logical*4 MPROP=t   True if proper motion is included
     *                     False if no proper motion is included
     * Integer*4 NMAG=1    Number of magnitudes present
     * Integer*4 NBENT=32  Number of bytes per star entry
     * Total : 28 bytes
     */
    size_t const headerSize = 28;

    /* Star record : 28 bytes for the dirSilesia/sao sample specifically
     * Real*4 XNO       Catalog number of star (not present, since stnum==0)
     * Real*8 SRA0      B1950 Right Ascension (radians)
     * Real*8 SDEC0     B1950 Declination (radians)
     * Character*2 IS   Spectral type (2 characters)
     * Integer*2 MAG    V Magnitude * 100
     * Real*4 XRPM      R.A. proper motion (radians per year)
     * Real*4 XDPM      Dec. proper motion (radians per year)
     */
    ZL_GraphID sra0 = ZL_Compressor_registerStaticGraph_fromPipelineNodes1o(
            cgraph,
            ZL_NODELIST(ZL_NODE_INTERPRET_TOKEN_AS_LE, ZL_NODE_DELTA_INT),
            ZL_GRAPH_FIELD_LZ);
    ZL_GraphID sdec0 = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph, ZL_NODE_TRANSPOSE_SPLIT, ZL_GRAPH_ZSTD);
    ZL_GraphID token_compress = ZL_Compressor_registerTokenizeGraph(
            cgraph,
            ZL_Type_struct,
            false,
            ZL_GRAPH_FIELD_LZ,
            ZL_GRAPH_FIELD_LZ);
    ZL_GraphID num_huffman = ZL_Compressor_registerStaticGraph_fromNode1o(
            cgraph,
            ZL_NODE_INTERPRET_TOKEN_AS_LE,
            ZL_Compressor_registerTokenizeGraph(
                    cgraph,
                    ZL_Type_numeric,
                    false,
                    ZL_GRAPH_HUFFMAN,
                    ZL_GRAPH_HUFFMAN));
    ZL_GraphID is             = num_huffman;
    ZL_GraphID mag            = num_huffman;
    ZL_GraphID xrpm           = token_compress;
    ZL_GraphID xdpm           = token_compress;
    ZL_GraphID splitStructure = ZL_Compressor_registerSplitByStructGraph(
            cgraph,
            (const size_t[]){ 8, 8, 2, 2, 4, 4 },
            (const ZL_GraphID[]){ sra0, sdec0, is, mag, xrpm, xdpm },
            6);

    return ZL_Compressor_registerSplitGraph(
            cgraph,
            ZL_Type_serial,
            (const size_t[]){ headerSize, 0 },
            (const ZL_GraphID[]){ ZL_GRAPH_STORE, splitStructure },
            2);
}
