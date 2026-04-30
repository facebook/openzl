// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once
#include <memory>
#include <string>
#include <vector>

#include "openzl/common/stream.h"
#include "openzl/cpp/CompressIntrospectionHooks.hpp"
#include "openzl/cpp/Input.hpp"
#include "tools/training/utils/utils.h"

namespace openzl::training {
class UntrainedGraphHook : public openzl::CompressIntrospectionHooks {
   public:
    explicit UntrainedGraphHook(std::vector<std::string> targetGraphNames)
            : CompressIntrospectionHooks(),
              targetGraphNames_(std::move(targetGraphNames))
    {
    }

    void on_migraphEncode_start(
            ZL_Graph* gctx,
            const ZL_Compressor* compressor,
            ZL_GraphID gid,
            ZL_Edge* inputs[],
            size_t nbInputs) override;

    const std::map<std::string, std::vector<MultiInput>>& getInputs() const;

   private:
    std::vector<std::string> targetGraphNames_;
    std::map<std::string, std::vector<MultiInput>> inputs_{};
    std::string errorMessage_{};
};

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

} // namespace openzl::training
