// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <memory>
#include <stdexcept>

#include "openzl/common/assertion.h"
#include "openzl/common/errors_internal.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_reflection.h"

namespace zstrong::bench::e2e::utils {

struct CGraphDeleter {
    void operator()(ZL_Compressor* cgraph) const
    {
        ZL_Compressor_free(cgraph);
    }
};

struct CCTXDeleter {
    void operator()(ZL_CCtx* cctx) const
    {
        ZL_CCtx_free(cctx);
    }
};

struct DCTXDeleter {
    void operator()(ZL_DCtx* dctx) const
    {
        ZL_DCtx_free(dctx);
    }
};

inline size_t ZS2_unwrap(ZL_Report r, const std::string& message)
{
    if (ZL_isError(r)) {
        throw std::runtime_error{ (message + ", ")
                                  + ZL_E_str(ZL_RES_error(r)) };
    }
    return ZL_validResult(r);
}

using CGraph_unique = std::unique_ptr<ZL_Compressor, utils::CGraphDeleter>;
using CCTX_unique   = std::unique_ptr<ZL_CCtx, utils::CCTXDeleter>;
using DCTX_unique   = std::unique_ptr<ZL_DCtx, utils::DCTXDeleter>;

/**
 * createCCTX:
 * Creates a CCTX and wraps it with a unique pointer that would free the CCTX
 * when its lifetime is over.
 * CCTX is set to have a default max format version.
 */
inline CCTX_unique createCCTX()
{
    CCTX_unique cctx(ZL_CCtx_create());
    if (cctx == nullptr) {
        throw std::bad_alloc{};
    }
    ZS2_unwrap(
            ZL_CCtx_setParameter(
                    cctx.get(), ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION),
            "Failed setting format version");
    return cctx;
}

/**
 * createDCTX:
 * Creates a DCTX and wraps it with a unique pointer that would free the DCTX
 * when its lifetime is over.
 */
inline DCTX_unique createDCTX()
{
    DCTX_unique dctx(ZL_DCtx_create());
    if (dctx == nullptr) {
        throw std::bad_alloc{};
    }
    return dctx;
}

/**
 * createCGraph:
 * Creates a CGraph and wraps it with a unique pointer that would free the
 * CGraph when its lifetime is over.
 */
inline CGraph_unique createCGraph()
{
    CGraph_unique cgraph(ZL_Compressor_create());
    if (cgraph == nullptr) {
        throw std::bad_alloc{};
    }
    return cgraph;
}

inline ZL_GraphID addConversionFromSerial(
        ZL_Compressor* cgraph,
        ZL_GraphID graph,
        size_t eltWidth)
{
    ZL_Type const inputType = ZL_Compressor_Graph_getInput0Mask(cgraph, graph);
    if (inputType & ZL_Type_serial) {
        return graph;
    }
    if (inputType & ZL_Type_struct) {
        ZL_IntParam intParams = { ZL_trlip_tokenSize, (int)eltWidth };
        ZL_LocalParams params = { .intParams = { &intParams, 1 } };
        const ZL_ParameterizedNodeDesc desc = {
            .node        = ZL_NODE_CONVERT_SERIAL_TO_TOKENX,
            .localParams = &params,
        };
        auto const node =
                ZL_Compressor_registerParameterizedNode(cgraph, &desc);
        return ZL_Compressor_registerStaticGraph_fromNode1o(
                cgraph, node, graph);
    }
    if (inputType & ZL_Type_numeric) {
        auto const node = [eltWidth] {
            switch (eltWidth) {
                case 1:
                    return ZL_NODE_INTERPRET_AS_LE8;
                case 2:
                    return ZL_NODE_INTERPRET_AS_LE16;
                case 4:
                    return ZL_NODE_INTERPRET_AS_LE32;
                case 8:
                    return ZL_NODE_INTERPRET_AS_LE64;
                default:
                    throw std::runtime_error{ "Bad elt width" };
            }
        }();
        return ZL_Compressor_registerStaticGraph_fromNode1o(
                cgraph, node, graph);
    }
    throw std::runtime_error(
            "Cannot automatically serialized into stream type");
}

} // namespace zstrong::bench::e2e::utils
