// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <iostream>

#include "openzl/cpp/CompressIntrospectionHooks.hpp"
#include "openzl/zl_reflection.h"

namespace openzl {

// until errors are propagated fully all the time, this is an alternative way to
// bubble the transform trace to the user
class DebugIntrospectionHooks : public openzl::CompressIntrospectionHooks {
   public:
    void on_codecEncode_start(
            ZL_Encoder*,
            const ZL_Compressor* compressor,
            ZL_NodeID nid,
            const ZL_Input*[],
            size_t) override
    {
        std::cerr << "codec start "
                  << ZL_Compressor_Node_getName(compressor, nid) << '\n';
    }

    void on_migraphEncode_start(
            ZL_Graph* gctx,
            const ZL_Compressor* compressor,
            ZL_GraphID gid,
            ZL_Edge*[],
            size_t nbInputs) override
    {
        std::cerr << "migraph start " << gctx << " "
                  << ZL_Compressor_Graph_getName(compressor, gid) << " "
                  << nbInputs << " inputs\n";
    }
    void on_migraphEncode_end(
            ZL_Graph* gctx,
            ZL_GraphID[],
            size_t nbSuccessors,
            ZL_Report) override
    {
        std::cerr << "migraph end " << gctx << " " << nbSuccessors
                  << " succs\n";
    }
};

} // namespace openzl
