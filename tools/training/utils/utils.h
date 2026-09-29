// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include "openzl/cpp/CCtx.hpp"
#include "openzl/cpp/Compressor.hpp"
#include "tools/io/InputSet.h"

namespace openzl::training {

/**
 * @brief Create a CCtx for training the compressor. The cctx is configured
 * so that if training is called multiple times, the parameters will not be
 * reset. Targets ZL_MAX_FORMAT_VERSION.
 */
CCtx refCCtxForTraining(const Compressor& compressor);

class MultiInput {
   public:
    explicit MultiInput(std::vector<Input>&& inputs = {})
            : inputs_(std::make_shared<std::vector<Input>>(std::move(inputs)))
    {
    }

    std::vector<Input>& operator*()
    {
        return *inputs_;
    }

    const std::vector<Input>& operator*() const
    {
        return *inputs_;
    }

    std::vector<Input>* operator->()
    {
        return inputs_.get();
    }

    const std::vector<Input>* operator->() const
    {
        return inputs_.get();
    }

    /**
     * @brief Returns maximum compressed size after compression using these
     * inputs.
     */
    size_t compressBound() const;

    // Adds input while not owning the buffer the input references
    void add(Input&& input)
    {
        inputs_->emplace_back(std::move(input));
    }

    // Adds input and ensures that the buffer the input references which is
    // owned by io::Input stays around by adding a reference to the shared ptr
    void add(std::shared_ptr<tools::io::Input> input)
    {
        inputSources_.emplace_back(input);
        add(openzl::Input::refSerial(input->contents()));
    }

   private:
    std::vector<std::shared_ptr<tools::io::Input>> inputSources_;
    std::shared_ptr<std::vector<Input>> inputs_;
};

/**
 * @brief Convert a set of inputs to a vector of MultiInputs. It is assumed that
 * each input is serial in @p inputs.
 */
std::vector<MultiInput> inputSetToMultiInputs(tools::io::InputSet& inputs);

/**
 * @brief Returns whether @p compressor is compatible with its configured
 * format version for every sample in @p inputs.
 *
 * It is the caller's responsibility to configure the compressor's format
 * version, select its starting graph, and provide inputs that exercise every
 * graph path whose compatibility must be tested. Compression errors unrelated
 * to format compatibility are ignored.
 */
bool compressorIsFormatCompatible(
        const Compressor& compressor,
        const std::vector<MultiInput>& inputs);

/**
 * @brief Filter @p graphs down to those able to compress @p inputs at the
 * target @p formatVersion.
 *
 * Each candidate graph is used to compress every sample in @p inputs. A graph
 * is filtered out if any compression reports a format-version incompatibility.
 * Supported graphs are returned in their original order.
 *
 * It is the caller's responsibility to provide inputs capable of exercising
 * every graph path whose format-version compatibility must be tested. A graph
 * that is incompatible with @p formatVersion may be retained if @p inputs do
 * not exercise the incompatible path.
 *
 * @throws Exception if @p formatVersion is less than ZL_MIN_FORMAT_VERSION.
 *
 * Standard graphs follow the guidelines which are required for this function to
 * work. Custom graphs are also required to follow these guidelines. These are
 * that graphs must either:
 * - Always select the same nodes and may not work on older format versions.
 * - Dynamically select which nodes to run, in which case they should be
 * format-version aware, meaning they should never execute a codec which
 * requires a format version above the library's format version.
 *
 * If these guidelines are not followed, the function may not correctly filter
 * out the graph.
 */
std::vector<GraphID> filterGraphsByFormatVersion(
        Compressor& compressor,
        const std::vector<GraphID>& graphs,
        const std::vector<MultiInput>& inputs);

} // namespace openzl::training
