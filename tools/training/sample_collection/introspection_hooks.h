// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "openzl/common/stream.h"
#include "openzl/cpp/CompressIntrospectionHooks.hpp"
#include "openzl/cpp/Input.hpp"
#include "tools/training/utils/utils.h"

namespace openzl::training {

class InputCopy : public Input {
   public:
    InputCopy(const ZL_Input* input)
            : Input(ZL_codemodMutDataAsInput(
                            STREAM_create(ZL_DATA_ID_INPUTSTREAM)),
                    ZL_TypedRef_free)
    {
        openzl::unwrap(
                STREAM_copy(
                        ZL_codemodMutInputAsData(get()),
                        ZL_codemodInputAsData(input)),
                "Failed to copy input data");
    }
};

/// Introspection hook that captures inputs flowing into targeted graphs
/// (via on_migraphEncode_start) and/or targeted codec nodes
/// (via on_codecEncode_start) in a single compression pass.
class SampleCollectionHook : public openzl::CompressIntrospectionHooks {
   public:
    SampleCollectionHook(
            std::vector<std::string> targetGraphNames,
            std::vector<std::string> targetNodeNames)
            : CompressIntrospectionHooks(),
              targetGraphNames_(std::move(targetGraphNames)),
              targetNodeNames_(std::move(targetNodeNames))
    {
    }

    void on_migraphEncode_start(
            ZL_Graph* gctx,
            const ZL_Compressor* compressor,
            ZL_GraphID gid,
            ZL_Edge* inputs[],
            size_t nbInputs) override;

    void on_codecEncode_start(
            ZL_Encoder* eictx,
            const ZL_Compressor* compressor,
            ZL_NodeID nid,
            const ZL_Input* inStreams[],
            size_t nbInStreams) override;

    const std::map<std::string, std::vector<MultiInput>>& getInputs() const;

   private:
    std::vector<std::string> targetGraphNames_;
    std::vector<std::string> targetNodeNames_;
    std::map<std::string, std::vector<MultiInput>> inputs_{};
    std::string errorMessage_{};
};

} // namespace openzl::training
