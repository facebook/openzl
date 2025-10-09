// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <optional>
#include <vector>

#include "openzl/zl_compressor.h"
#include "openzl/zl_ctransform.h"
#include "openzl/zl_decompress.h"
#include "openzl/zl_dtransform.h"
#include "openzl/zl_opaque_types.h"
#include "openzl/zl_selector.h"

#include "openzl/common/debug.h"
#include "openzl/common/stream.h"
#include "openzl/zl_data.h"

namespace zstrong {
namespace tests {

struct ZL_TypedRef_Deleter {
    void operator()(ZL_TypedRef* tref) const noexcept
    {
        ZL_TypedRef_free(tref);
    }
};

// This class wraps data and exports a ZL_Data.
// It exists to preserve memory safety when creating streams during tests.
template <typename T>
class WrappedStream {
   public:
    WrappedStream(const std::vector<T>& data, ZL_Type type) : ownedData(data)
    {
        initStream(type);
    }
    ~WrappedStream()
    {
        STREAM_free(stream);
    }
    const ZL_Input* getStream()
    {
        return ZL_codemodDataAsInput(stream);
    }

   private:
    void initStream(ZL_Type type)
    {
        stream = STREAM_create(ZL_DATA_ID_INPUTSTREAM);
        size_t nbElts;
        size_t eltWidth;
        if (type == ZL_Type_serial) {
            nbElts   = ownedData.size() * sizeof(T);
            eltWidth = 1;
        } else if (type == ZL_Type_struct || type == ZL_Type_numeric) {
            nbElts   = ownedData.size();
            eltWidth = sizeof(T);
        } else {
            ZL_REQUIRE_FAIL("Bad stream type");
        }
        ZL_REQUIRE_SUCCESS(STREAM_refConstBuffer(
                stream, ownedData.data(), type, eltWidth, nbElts));
    }
    const std::vector<T> ownedData;
    ZL_Data /* terrelln-codemod-skip */* stream;
};

/// Base clase for all the zstrong tests.
/// Contains helpers for setting up a graph, registering custom transforms,
/// and running a round trip test.
class ZStrongTest : public testing::Test {
   public:
    ~ZStrongTest();

    struct TypedInputDesc {
        std::string data;
        ZL_Type type;
        size_t eltWidth;
        std::vector<uint32_t> strLens;
    };

    /// Reset the state to run another test
    virtual void reset();

    /// Declare an Graph from a Node with the given output node.
    ZL_GraphID declareGraph(ZL_NodeID node, ZL_GraphID graph);

    /// Declare an Graph from a Node with the given output nodes.
    ZL_GraphID declareGraph(
            ZL_NodeID node,
            std::vector<ZL_GraphID> const& graphs);

    /// Declare a Graph with the given node. Trivial
    /// graphs will be created for each output stream.
    ZL_GraphID declareGraph(ZL_NodeID node);

    ZL_GraphID declareSelectorGraph(
            ZL_SelectorFn selectorFunc,
            const std::vector<ZL_GraphID>& graphs);

    /// Register the custom transform and set it as the node to test
    ZL_NodeID registerCustomTransform(
            ZL_TypedEncoderDesc const& compress,
            ZL_TypedDecoderDesc const& decompress);

    /// Register the custom transform and set it as the node to test
    ZL_NodeID registerCustomTransform(
            ZL_SplitEncoderDesc const& compress,
            ZL_SplitDecoderDesc const& decompress);

    /// Register the custom transform and set it as the node to test
    ZL_NodeID registerCustomTransform(
            ZL_PipeEncoderDesc const& compress,
            ZL_PipeDecoderDesc const& decompress);

    // Creates a node with specific local parameters
    ZL_NodeID createParameterizedNode(
            ZL_NodeID node,
            const ZL_LocalParams& localParams);

    /// Finalize the graph with the given graph
    ZL_Compressor* finalizeGraph(ZL_GraphID graph, size_t inEltWidth);

    void setVsfFieldSizes(std::vector<uint32_t> fieldSizes);

    void setParameter(ZL_CParam param, int value);

    /// Sets (de)compression for the graph
    void setLevels(int compressionLevel = 0, int decompressionLevel = 0);

    /// Returns the compress size bounds for a give data
    size_t compressBounds(std::string_view data);

    /// Sets a mode where compress bounds are actually big enough to contain
    /// @factor times the source data. Useful when a specifc tested transform
    /// might expand the data.
    void setLargeCompressBound(size_t factor);

    // (de)Compression functions, return a pair of return code and output as
    // std::string.
    // If return code is error, he second item may be optnone.
    std::pair<ZL_Report, std::optional<std::string>> compress(
            std::string_view data);
    std::pair<ZL_Report, std::optional<std::string>> decompress(
            std::string_view data);
    std::pair<ZL_Report, std::vector<ZL_TypedBuffer*>> decompressMI(
            std::string_view data);

    std::pair<ZL_Report, std::optional<std::string>> compressTyped(
            ZL_TypedRef* typedRef);
    std::pair<ZL_Report, std::optional<std::string>> compressMI(
            std::vector<std::unique_ptr<ZL_TypedRef, ZL_TypedRef_Deleter>>&
                    inputs);

    void assertEqual(const ZL_TypedBuffer* buffer, const TypedInputDesc& desc);

    void testRoundTripMI(
            std::vector<std::unique_ptr<ZL_TypedRef, ZL_TypedRef_Deleter>>&
                    inputs,
            std::vector<TypedInputDesc>& inputDescs)
    {
        return testRoundTripMIImpl(inputs, inputDescs, false);
    }

    void testRoundTripMICompressionMayFail(
            std::vector<std::unique_ptr<ZL_TypedRef, ZL_TypedRef_Deleter>>&
                    inputs,
            std::vector<TypedInputDesc>& inputDescs)
    {
        return testRoundTripMIImpl(inputs, inputDescs, true);
    }

    /// Test that a single input round trips after finalizing the graph
    void testRoundTrip(std::string_view data)
    {
        return testRoundTripImpl(data, false);
    }

    void testRoundTripCompressionMayFail(std::string_view data)
    {
        return testRoundTripImpl(data, true);
    }

    /// Converts a graph that gets a typed stream into one that accepts a
    /// serialzed stream
    ZL_GraphID
    convertSerializedToType(ZL_Type type, size_t eltWidth, ZL_GraphID graph);

    /// Explicitly set a stream type to convert to for testing (useful for
    /// multi-type)
    void setStreamInType(ZL_Type type);

    void setFormatVersion(unsigned formatVersion)
    {
        formatVersion_ = formatVersion;
    }

    /// @returns a graph that stores the given stream type
    ZL_GraphID store(ZL_Type type);

   private:
    void testRoundTripImpl(std::string_view data, bool compressionMayFail);
    void testRoundTripMIImpl(
            std::vector<std::unique_ptr<ZL_TypedRef, ZL_TypedRef_Deleter>>&
                    inputs,
            std::vector<TypedInputDesc>& inputDescs,
            bool compressionMayFail);

   protected:
    ZL_CCtx* cctx_{ nullptr };
    ZL_Compressor* cgraph_{ nullptr };
    ZL_DCtx* dctx_{ nullptr };
    size_t eltWidth_;
    size_t compressBoundFactor_{ 1 };
    std::optional<ZL_Type> inType_;
    std::optional<unsigned> formatVersion_;
    std::vector<uint32_t> fieldSizes_;
    ZL_SetStringLensInstructions vsfFieldSizesInstructs_{};
};

} // namespace tests
} // namespace zstrong
