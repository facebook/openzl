// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <array>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "openzl/codecs/zl_sparse_num.h"
#include "openzl/common/allocation.h"
#include "openzl/common/stream.h"
#include "openzl/compress/cctx.h"
#include "openzl/compress/cgraph.h"
#include "openzl/compress/cnode.h"
#include "openzl/compress/codec_output_cache.h"
#include "openzl/compress/enc_interface.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/zl_codec_output_cache.h"
#include "openzl/zl_compress.h"
#include "openzl/zl_compressor.h"
#include "openzl/zl_version.h"

namespace {

class Cache {
   public:
    Cache() : cache_(CodecCache_create(CodecCache_getDefaultMaxBytes()))
    {
        if (cache_ != nullptr) {
            CodecCache_setStatsEnabled(cache_, true);
        }
    }

    explicit Cache(size_t maxBytes) : cache_(CodecCache_create(maxBytes))
    {
        if (cache_ != nullptr) {
            CodecCache_setStatsEnabled(cache_, true);
        }
    }

    ~Cache()
    {
        CodecCache_free(cache_);
    }

    ZL_CodecOutputCache* get() const
    {
        return cache_;
    }

   private:
    ZL_CodecOutputCache* cache_;
};

CodecCache_Output makeOutput(const void* content, size_t size)
{
    return CodecCache_Output{
        .type         = ZL_Type_serial,
        .outcomeIndex = 0,
        .eltWidth     = 1,
        .numElts      = size,
        .contentSize  = size,
        .content      = content,
    };
}

CodecCache_Result makeResult(
        const CodecCache_Output* outputs,
        size_t nbOutputs,
        const void* header = nullptr,
        size_t headerSize  = 0)
{
    return CodecCache_Result{
        .nbOutputs  = nbOutputs,
        .outputs    = outputs,
        .headerSize = headerSize,
        .header     = header,
    };
}

class CodecOutputCacheTest : public testing::Test {
   protected:
    void SetUp() override
    {
        compressor_   = ZL_Compressor_create();
        cctx_         = ZL_CCtx_create();
        scratchArena_ = ALLOC_HeapArena_create();
        ASSERT_NE(compressor_, nullptr);
        ASSERT_NE(cctx_, nullptr);
        ASSERT_NE(scratchArena_, nullptr);
        ASSERT_FALSE(ZL_isError(ZL_Compressor_selectStartingGraphID(
                compressor_, ZL_GRAPH_RANGE_PACK_ZSTD)));
        ASSERT_FALSE(ZL_isError(ZL_CCtx_refCompressor(cctx_, compressor_)));
        encoder_.cctx      = cctx_;
        encoder_.wkspArena = scratchArena_;
    }

    void TearDown() override
    {
        ALLOC_Arena_freeArena(scratchArena_);
        ZL_CCtx_free(cctx_);
        ZL_Compressor_free(compressor_);
    }

    Stream* makeInput(const void* content, size_t size)
    {
        Stream* const input =
                STREAM_createInArena(scratchArena_, ZL_DATA_ID_INPUTSTREAM);
        if (input == nullptr
            || ZL_isError(STREAM_refConstBuffer(
                    input, content, ZL_Type_serial, 1, size))) {
            return nullptr;
        }
        return input;
    }

    CodecCache_Lookup* lookupWithParams(
            ZL_CodecOutputCache* cache,
            ZL_NodeID node,
            const ZL_Data* input,
            const ZL_LocalParams* localParams)
    {
        encoder_.cnode   = CGRAPH_getCNode(compressor_, node);
        encoder_.lparams = localParams;
        return CodecCache_lookup(cache, &encoder_, node, input);
    }

    CodecCache_Lookup*
    lookup(ZL_CodecOutputCache* cache, ZL_NodeID node, const ZL_Data* input)
    {
        const CNode* const cnode = CGRAPH_getCNode(compressor_, node);
        return lookupWithParams(
                cache,
                node,
                input,
                &cnode->transformDesc.publicDesc.localParams);
    }

    bool setCodecCParams(
            int formatVersion,
            int compressionLevel,
            int decompressionLevel)
    {
        return !ZL_isError(ZL_CCtx_setParameter(
                       cctx_, ZL_CParam_formatVersion, formatVersion))
                && !ZL_isError(ZL_CCtx_setParameter(
                        cctx_, ZL_CParam_compressionLevel, compressionLevel))
                && !ZL_isError(ZL_CCtx_setParameter(
                        cctx_,
                        ZL_CParam_decompressionLevel,
                        decompressionLevel))
                && !ZL_isError(CCTX_setAppliedParameters(cctx_));
    }

    ZL_Compressor* compressor_{ nullptr };
    ZL_CCtx* cctx_{ nullptr };
    Arena* scratchArena_{ nullptr };
    ZL_Encoder encoder_{};
};

TEST_F(CodecOutputCacheTest, CreateEmpty)
{
    Cache cache;
    ASSERT_NE(cache.get(), nullptr);
    const CodecCache_Stats stats = CodecCache_getStats(cache.get());
    EXPECT_EQ(stats.hits, 0);
    EXPECT_EQ(stats.misses, 0);
    EXPECT_EQ(stats.inserts, 0);
    EXPECT_EQ(stats.bytesStored, 0);
}

TEST_F(CodecOutputCacheTest, StatisticsAreDisabledByDefault)
{
    std::unique_ptr<ZL_CodecOutputCache, decltype(&CodecCache_free)> cache(
            CodecCache_create(CodecCache_getDefaultMaxBytes()),
            &CodecCache_free);
    ASSERT_NE(cache, nullptr);
    const Stream* const input = makeInput("input", 5);
    ASSERT_NE(input, nullptr);
    CodecCache_Lookup* const miss =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(miss, nullptr);
    const CodecCache_Output output = makeOutput("A", 1);
    const CodecCache_Result result = makeResult(&output, 1);
    ASSERT_EQ(
            CodecCache_store(miss, &result), CodecCache_InsertResult_inserted);
    CodecCache_Lookup* const hit =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(hit, nullptr);
    ASSERT_NE(CodecCache_Lookup_getResult(hit), nullptr);

    const CodecCache_Stats stats = CodecCache_getStats(cache.get());
    EXPECT_EQ(stats.hits, 0);
    EXPECT_EQ(stats.misses, 0);
    EXPECT_EQ(stats.inserts, 0);
    EXPECT_EQ(stats.bytesStored, 0);
    EXPECT_EQ(stats.arenaBytes, 0);
}

TEST_F(CodecOutputCacheTest, StoreThenLookupReturnsResult)
{
    Cache cache;
    const char inputBytes[]   = "input";
    const char outputBytes[]  = "output";
    const char header[]       = "header";
    const Stream* const input = makeInput(inputBytes, sizeof(inputBytes));
    ASSERT_NE(input, nullptr);
    CodecCache_Lookup* const miss =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(miss, nullptr);
    EXPECT_EQ(CodecCache_Lookup_getResult(miss), nullptr);
    const CodecCache_Output output =
            makeOutput(outputBytes, sizeof(outputBytes));
    const CodecCache_Result result =
            makeResult(&output, 1, header, sizeof(header));
    ASSERT_EQ(
            CodecCache_store(miss, &result), CodecCache_InsertResult_inserted);

    CodecCache_Lookup* const hit =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(hit, nullptr);
    const CodecCache_Result* const cachedResult =
            CodecCache_Lookup_getResult(hit);
    ASSERT_NE(cachedResult, nullptr);
    ASSERT_EQ(cachedResult->nbOutputs, 1);
    EXPECT_EQ(cachedResult->outputs[0].contentSize, sizeof(outputBytes));
    EXPECT_EQ(
            std::memcmp(
                    cachedResult->outputs[0].content,
                    outputBytes,
                    sizeof(outputBytes)),
            0);
    EXPECT_EQ(cachedResult->headerSize, sizeof(header));
    EXPECT_EQ(std::memcmp(cachedResult->header, header, sizeof(header)), 0);
}

TEST_F(CodecOutputCacheTest, DifferentInputsDoNotShareResult)
{
    Cache cache;
    const Stream* const firstInput  = makeInput("first", 5);
    const Stream* const secondInput = makeInput("other", 5);
    ASSERT_NE(firstInput, nullptr);
    ASSERT_NE(secondInput, nullptr);
    CodecCache_Lookup* const first =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, firstInput);
    ASSERT_NE(first, nullptr);
    const CodecCache_Output output = makeOutput("A", 1);
    const CodecCache_Result result = makeResult(&output, 1);
    ASSERT_EQ(
            CodecCache_store(first, &result), CodecCache_InsertResult_inserted);

    CodecCache_Lookup* const second =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, secondInput);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(CodecCache_Lookup_getResult(second), nullptr);
}

TEST_F(CodecOutputCacheTest, DifferentInputMetadataDoesNotShareResult)
{
    Cache cache;
    Stream* const firstInput  = makeInput("input", 5);
    Stream* const secondInput = makeInput("input", 5);
    ASSERT_NE(firstInput, nullptr);
    ASSERT_NE(secondInput, nullptr);
    ASSERT_FALSE(ZL_isError(STREAM_setIntMetadata(firstInput, 1, 2)));
    ASSERT_FALSE(ZL_isError(STREAM_setIntMetadata(secondInput, 1, 3)));

    CodecCache_Lookup* const first =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, firstInput);
    ASSERT_NE(first, nullptr);
    const CodecCache_Output output = makeOutput("A", 1);
    const CodecCache_Result result = makeResult(&output, 1);
    ASSERT_EQ(
            CodecCache_store(first, &result), CodecCache_InsertResult_inserted);

    CodecCache_Lookup* const second =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, secondInput);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(CodecCache_Lookup_getResult(second), nullptr);
    STREAM_free(firstInput);
    STREAM_free(secondInput);
}

TEST_F(CodecOutputCacheTest, DifferentCodecCParamsDoNotShareResult)
{
    constexpr int kFormatVersion           = ZL_MAX_FORMAT_VERSION;
    constexpr int kCompressionLevel        = 6;
    constexpr int kDecompressionLevel      = 3;
    constexpr int kOtherFormatVersion      = ZL_MIN_FORMAT_VERSION;
    constexpr int kOtherCompressionLevel   = 5;
    constexpr int kOtherDecompressionLevel = 2;
    static_assert(kFormatVersion != kOtherFormatVersion);

    ASSERT_TRUE(setCodecCParams(
            kFormatVersion, kCompressionLevel, kDecompressionLevel));
    Cache cache;
    const Stream* const input = makeInput("input", 5);
    ASSERT_NE(input, nullptr);
    CodecCache_Lookup* const first =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(first, nullptr);
    const CodecCache_Output output = makeOutput("A", 1);
    const CodecCache_Result result = makeResult(&output, 1);
    ASSERT_EQ(
            CodecCache_store(first, &result), CodecCache_InsertResult_inserted);

    const std::array<std::array<int, 3>, 3> alternatives = { {
            { kOtherFormatVersion, kCompressionLevel, kDecompressionLevel },
            { kFormatVersion, kOtherCompressionLevel, kDecompressionLevel },
            { kFormatVersion, kCompressionLevel, kOtherDecompressionLevel },
    } };
    for (const auto& params : alternatives) {
        ASSERT_TRUE(setCodecCParams(params[0], params[1], params[2]));
        CodecCache_Lookup* const lookupResult =
                lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
        ASSERT_NE(lookupResult, nullptr);
        EXPECT_EQ(CodecCache_Lookup_getResult(lookupResult), nullptr);
    }
}

TEST_F(CodecOutputCacheTest, ReferenceParamsAreNotCacheable)
{
    const int referencedValue  = 42;
    const ZL_RefParam refParam = {
        .paramId   = 1,
        .paramRef  = &referencedValue,
        .paramSize = sizeof(referencedValue),
    };
    ZL_LocalParams localParams{};
    localParams.refParams = { &refParam, 1 };

    Cache cache;
    const Stream* const input = makeInput("input", 5);
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(
            lookupWithParams(
                    cache.get(), ZL_NODE_SPARSE_NUM, input, &localParams),
            nullptr);
    EXPECT_EQ(CodecCache_getStats(cache.get()).refParamSkips, 1);
}

TEST_F(CodecOutputCacheTest, CustomCodecIsNotCacheable)
{
    const ZL_Type outputType = ZL_Type_serial;
    const ZL_TypedEncoderDesc desc = {
        .gd = {
            .CTid = 1,
            .inStreamType = ZL_Type_serial,
            .outStreamTypes = &outputType,
            .nbOutStreams = 1,
        },
        .transform_f =
                [](ZL_Encoder*, const ZL_Input*) noexcept {
                    return ZL_returnSuccess();
                },
        .name = "custom_codec_cache_test",
    };
    const ZL_NodeID customNode =
            ZL_Compressor_registerTypedEncoder(compressor_, &desc);
    ASSERT_NE(customNode.nid, ZL_NODE_ILLEGAL.nid);

    Cache cache;
    const Stream* const input = makeInput("input", 5);
    ASSERT_NE(input, nullptr);
    EXPECT_EQ(lookup(cache.get(), customNode, input), nullptr);
    EXPECT_EQ(CodecCache_getStats(cache.get()).customCodecSkips, 1);
}

TEST_F(CodecOutputCacheTest, DifferentLocalParamPlanesDoNotShareResult)
{
    constexpr size_t kIntsPerSizeT = sizeof(size_t) / sizeof(int);
    static_assert(sizeof(size_t) % sizeof(int) == 0);
    // These are the words produced by two zero-sized copy parameters.
    std::array<int, 2 * (1 + kIntsPerSizeT)> serializedWords{};
    serializedWords[0]                 = 7;
    serializedWords[1 + kIntsPerSizeT] = 11;

    std::array<ZL_IntParam, 1 + kIntsPerSizeT> intParams{};
    for (size_t i = 0; i < intParams.size(); ++i) {
        intParams[i] = { serializedWords[2 * i], serializedWords[2 * i + 1] };
    }
    ZL_LocalParams intLocalParams{};
    intLocalParams.intParams = { intParams.data(), intParams.size() };

    const std::array<ZL_CopyParam, 2> copyParams = {
        ZL_CopyParam{ 7, nullptr, 0 },
        ZL_CopyParam{ 11, nullptr, 0 },
    };
    ZL_LocalParams copyLocalParams{};
    copyLocalParams.copyParams = { copyParams.data(), copyParams.size() };

    Cache cache;
    const Stream* const input = makeInput("input", 5);
    ASSERT_NE(input, nullptr);
    CodecCache_Lookup* const first = lookupWithParams(
            cache.get(), ZL_NODE_SPARSE_NUM, input, &intLocalParams);
    ASSERT_NE(first, nullptr);
    const CodecCache_Output output = makeOutput("A", 1);
    const CodecCache_Result result = makeResult(&output, 1);
    ASSERT_EQ(
            CodecCache_store(first, &result), CodecCache_InsertResult_inserted);

    CodecCache_Lookup* const second = lookupWithParams(
            cache.get(), ZL_NODE_SPARSE_NUM, input, &copyLocalParams);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(CodecCache_Lookup_getResult(second), nullptr);
}

TEST_F(CodecOutputCacheTest, DifferentIntParamValuesDoNotShareResult)
{
    const ZL_IntParam firstParam  = { 7, 11 };
    const ZL_IntParam secondParam = { 7, 12 };
    ZL_LocalParams firstParams{};
    firstParams.intParams = { &firstParam, 1 };
    ZL_LocalParams secondParams{};
    secondParams.intParams = { &secondParam, 1 };

    Cache cache;
    const Stream* const input = makeInput("input", 5);
    ASSERT_NE(input, nullptr);
    CodecCache_Lookup* const first = lookupWithParams(
            cache.get(), ZL_NODE_SPARSE_NUM, input, &firstParams);
    ASSERT_NE(first, nullptr);
    const CodecCache_Output output = makeOutput("A", 1);
    const CodecCache_Result result = makeResult(&output, 1);
    ASSERT_EQ(
            CodecCache_store(first, &result), CodecCache_InsertResult_inserted);

    CodecCache_Lookup* const second = lookupWithParams(
            cache.get(), ZL_NODE_SPARSE_NUM, input, &secondParams);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(CodecCache_Lookup_getResult(second), nullptr);
}

TEST_F(CodecOutputCacheTest, DifferentCopyParamValuesDoNotShareResult)
{
    const char firstValue[]        = "first";
    const char secondValue[]       = "other";
    const ZL_CopyParam firstParam  = { 7, firstValue, sizeof(firstValue) };
    const ZL_CopyParam secondParam = { 7, secondValue, sizeof(secondValue) };
    ZL_LocalParams firstParams{};
    firstParams.copyParams = { &firstParam, 1 };
    ZL_LocalParams secondParams{};
    secondParams.copyParams = { &secondParam, 1 };

    Cache cache;
    const Stream* const input = makeInput("input", 5);
    ASSERT_NE(input, nullptr);
    CodecCache_Lookup* const first = lookupWithParams(
            cache.get(), ZL_NODE_SPARSE_NUM, input, &firstParams);
    ASSERT_NE(first, nullptr);
    const CodecCache_Output output = makeOutput("A", 1);
    const CodecCache_Result result = makeResult(&output, 1);
    ASSERT_EQ(
            CodecCache_store(first, &result), CodecCache_InsertResult_inserted);

    CodecCache_Lookup* const second = lookupWithParams(
            cache.get(), ZL_NODE_SPARSE_NUM, input, &secondParams);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(CodecCache_Lookup_getResult(second), nullptr);
}

TEST_F(CodecOutputCacheTest, DisabledInsertionsStillAllowHits)
{
    Cache cache;
    const Stream* const storedInput = makeInput("stored", 6);
    const Stream* const newInput    = makeInput("new", 3);
    ASSERT_NE(storedInput, nullptr);
    ASSERT_NE(newInput, nullptr);
    CodecCache_Lookup* const miss =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, storedInput);
    ASSERT_NE(miss, nullptr);
    const CodecCache_Output output = makeOutput("output", 6);
    const CodecCache_Result result = makeResult(&output, 1);
    ASSERT_EQ(
            CodecCache_store(miss, &result), CodecCache_InsertResult_inserted);

    CodecCache_setInsertionsEnabled(cache.get(), false);
    CodecCache_Lookup* const hit =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, storedInput);
    ASSERT_NE(hit, nullptr);
    EXPECT_NE(CodecCache_Lookup_getResult(hit), nullptr);
    EXPECT_EQ(lookup(cache.get(), ZL_NODE_SPARSE_NUM, newInput), nullptr);
    EXPECT_EQ(CodecCache_getStats(cache.get()).inserts, 1);
}

TEST_F(CodecOutputCacheTest, MemoizedHashCollisionStillComparesExactInputs)
{
    Cache cache;
    Stream* const firstInput  = makeInput("first", 5);
    Stream* const secondInput = makeInput("other", 5);
    ASSERT_NE(firstInput, nullptr);
    ASSERT_NE(secondInput, nullptr);
    STREAM_setCodecCacheKeyHash(firstInput, 7);
    STREAM_setCodecCacheKeyHash(secondInput, 7);
    CodecCache_Lookup* const first =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, firstInput);
    ASSERT_NE(first, nullptr);
    const CodecCache_Output output = makeOutput("A", 1);
    const CodecCache_Result result = makeResult(&output, 1);
    ASSERT_EQ(
            CodecCache_store(first, &result), CodecCache_InsertResult_inserted);

    CodecCache_Lookup* const second =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, secondInput);
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(CodecCache_Lookup_getResult(second), nullptr);
}

TEST_F(CodecOutputCacheTest, EncoderNodeNotWireTransformIdentifiesInvocation)
{
    Cache cache;
    ASSERT_EQ(
            ZL_Compressor_Node_getCodecID(compressor_, ZL_NODE_SPARSE_NUM),
            ZL_Compressor_Node_getCodecID(
                    compressor_, ZL_NODE_SPARSE_NUM_AUTO));
    const Stream* const input = makeInput("same input", 10);
    ASSERT_NE(input, nullptr);
    CodecCache_Lookup* const sparse =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(sparse, nullptr);
    const CodecCache_Output output = makeOutput("A", 1);
    const CodecCache_Result result = makeResult(&output, 1);
    ASSERT_EQ(
            CodecCache_store(sparse, &result),
            CodecCache_InsertResult_inserted);

    CodecCache_Lookup* const sparseAuto =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM_AUTO, input);
    ASSERT_NE(sparseAuto, nullptr);
    EXPECT_EQ(CodecCache_Lookup_getResult(sparseAuto), nullptr);
}

TEST_F(CodecOutputCacheTest, StoredInvocationAndResultOwnBorrowedData)
{
    Cache cache;
    char inputBytes[]                   = "input";
    char outputBytes[]                  = "output";
    char header[]                       = "header";
    Stream_IntMetadata outputMetadata[] = { { 3, 4 } };
    Stream* const input = makeInput(inputBytes, sizeof(inputBytes));
    ASSERT_NE(input, nullptr);
    ASSERT_FALSE(ZL_isError(STREAM_setIntMetadata(input, 1, 2)));
    CodecCache_Lookup* const miss =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(miss, nullptr);
    CodecCache_Output output = makeOutput(outputBytes, sizeof(outputBytes));
    output.nbIntMetadata     = 1;
    output.intMetadata       = outputMetadata;
    const CodecCache_Result result =
            makeResult(&output, 1, header, sizeof(header));
    ASSERT_EQ(
            CodecCache_store(miss, &result), CodecCache_InsertResult_inserted);

    std::memset(inputBytes, 'x', sizeof(inputBytes));
    std::memset(outputBytes, 'x', sizeof(outputBytes));
    std::memset(header, 'x', sizeof(header));
    outputMetadata[0] = { 9, 9 };
    STREAM_free(input);
    ALLOC_Arena_freeAll(scratchArena_);

    const char originalInput[] = "input";
    Stream* const lookupInput = makeInput(originalInput, sizeof(originalInput));
    ASSERT_NE(lookupInput, nullptr);
    ASSERT_FALSE(ZL_isError(STREAM_setIntMetadata(lookupInput, 1, 2)));
    CodecCache_Lookup* const hit =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, lookupInput);
    ASSERT_NE(hit, nullptr);
    const CodecCache_Result* const cachedResult =
            CodecCache_Lookup_getResult(hit);
    ASSERT_NE(cachedResult, nullptr);
    const char originalOutput[] = "output";
    const char originalHeader[] = "header";
    EXPECT_EQ(
            std::memcmp(
                    cachedResult->outputs[0].content,
                    originalOutput,
                    sizeof(originalOutput)),
            0);
    EXPECT_EQ(cachedResult->outputs[0].intMetadata[0].id, 3);
    EXPECT_EQ(cachedResult->outputs[0].intMetadata[0].value, 4);
    EXPECT_EQ(
            std::memcmp(
                    cachedResult->header,
                    originalHeader,
                    sizeof(originalHeader)),
            0);
    STREAM_free(lookupInput);
}

TEST_F(CodecOutputCacheTest, DuplicateStoreKeepsOriginal)
{
    Cache cache;
    const Stream* const input = makeInput("input", 5);
    ASSERT_NE(input, nullptr);
    CodecCache_Lookup* const miss =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(miss, nullptr);
    const CodecCache_Output firstOutput  = makeOutput("A", 1);
    const CodecCache_Output secondOutput = makeOutput("B", 1);
    const CodecCache_Result firstResult  = makeResult(&firstOutput, 1);
    const CodecCache_Result secondResult = makeResult(&secondOutput, 1);
    EXPECT_EQ(
            CodecCache_store(miss, &firstResult),
            CodecCache_InsertResult_inserted);
    EXPECT_EQ(
            CodecCache_store(miss, &secondResult),
            CodecCache_InsertResult_duplicate);
    const CodecCache_Stats stats = CodecCache_getStats(cache.get());

    CodecCache_Lookup* const hit =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(hit, nullptr);
    const CodecCache_Result* const cachedResult =
            CodecCache_Lookup_getResult(hit);
    ASSERT_NE(cachedResult, nullptr);
    EXPECT_EQ(*static_cast<const char*>(cachedResult->outputs[0].content), 'A');
    EXPECT_EQ(stats.duplicateInserts, 1);
}

TEST_F(CodecOutputCacheTest, BudgetExhaustionSkipsStore)
{
    Cache cache(8);
    const Stream* const input = makeInput("input", 5);
    ASSERT_NE(input, nullptr);
    CodecCache_Lookup* const miss =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(miss, nullptr);
    const CodecCache_Output output = makeOutput("output", 6);
    const CodecCache_Result result = makeResult(&output, 1);
    EXPECT_EQ(
            CodecCache_store(miss, &result),
            CodecCache_InsertResult_budgetExceeded);
    const CodecCache_Stats stats = CodecCache_getStats(cache.get());
    EXPECT_EQ(stats.budgetSkips, 1);
    EXPECT_EQ(stats.bytesStored, 0);
}

TEST_F(CodecOutputCacheTest, StringOutputIsNotCacheable)
{
    Cache cache;
    const Stream* const input = makeInput("input", 5);
    ASSERT_NE(input, nullptr);
    CodecCache_Lookup* const miss =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(miss, nullptr);
    CodecCache_Output output       = makeOutput("output", 6);
    output.type                    = ZL_Type_string;
    const CodecCache_Result result = makeResult(&output, 1);

    EXPECT_EQ(
            CodecCache_store(miss, &result),
            CodecCache_InsertResult_notCacheable);
    const CodecCache_Stats stats = CodecCache_getStats(cache.get());
    EXPECT_EQ(stats.stringSkips, 1);
    EXPECT_EQ(stats.inserts, 0);
    EXPECT_EQ(stats.bytesStored, 0);

    CodecCache_Lookup* const afterStore =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(afterStore, nullptr);
    EXPECT_EQ(CodecCache_Lookup_getResult(afterStore), nullptr);
}

TEST_F(CodecOutputCacheTest, ResetDropsEntriesAndCounters)
{
    Cache cache;
    const Stream* const input = makeInput("input", 5);
    ASSERT_NE(input, nullptr);
    CodecCache_Lookup* const miss =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(miss, nullptr);
    const CodecCache_Output output = makeOutput("output", 6);
    const CodecCache_Result result = makeResult(&output, 1);
    ASSERT_EQ(
            CodecCache_store(miss, &result), CodecCache_InsertResult_inserted);

    CodecCache_reset(cache.get());
    const CodecCache_Stats stats = CodecCache_getStats(cache.get());
    EXPECT_EQ(stats.hits, 0);
    EXPECT_EQ(stats.misses, 0);
    EXPECT_EQ(stats.inserts, 0);
    EXPECT_EQ(stats.bytesStored, 0);
    CodecCache_Lookup* const afterReset =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(afterReset, nullptr);
    EXPECT_EQ(CodecCache_Lookup_getResult(afterReset), nullptr);
}

TEST_F(CodecOutputCacheTest, ZeroOutputResult)
{
    Cache cache;
    const Stream* const input = makeInput("input", 5);
    ASSERT_NE(input, nullptr);
    CodecCache_Lookup* const miss =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(miss, nullptr);
    const CodecCache_Result result = makeResult(nullptr, 0);
    ASSERT_EQ(
            CodecCache_store(miss, &result), CodecCache_InsertResult_inserted);
    CodecCache_Lookup* const hit =
            lookup(cache.get(), ZL_NODE_SPARSE_NUM, input);
    ASSERT_NE(hit, nullptr);
    const CodecCache_Result* const cachedResult =
            CodecCache_Lookup_getResult(hit);
    ASSERT_NE(cachedResult, nullptr);
    EXPECT_EQ(cachedResult->nbOutputs, 0);
    EXPECT_EQ(cachedResult->headerSize, 0);
    EXPECT_EQ(cachedResult->header, nullptr);
}

TEST(CodecOutputCacheLifecycleTest, NullOperationsAreSafe)
{
    CodecCache_free(nullptr);
    CodecCache_reset(nullptr);
}

TEST(CodecOutputCacheIntegrationTest, AttachedCacheReplaysAndReusesOutputHashes)
{
    ZL_Compressor* const compressor = ZL_Compressor_create();
    ASSERT_NE(compressor, nullptr);
    ASSERT_FALSE(ZL_isError(ZL_Compressor_selectStartingGraphID(
            compressor, ZL_GRAPH_RANGE_PACK_ZSTD)));
    ZL_CCtx* const cctx = ZL_CCtx_create();
    ASSERT_NE(cctx, nullptr);
    ASSERT_FALSE(ZL_isError(ZL_CCtx_refCompressor(cctx, compressor)));
    ASSERT_FALSE(ZL_isError(ZL_CCtx_setParameter(
            cctx, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION)));
    ASSERT_FALSE(ZL_isError(
            ZL_CCtx_setParameter(cctx, ZL_CParam_stickyParameters, 1)));
    ZL_CodecOutputCache* const cache = ZL_CodecOutputCache_create();
    ASSERT_NE(cache, nullptr);
    CodecCache_setStatsEnabled(cache, true);
    ASSERT_FALSE(ZL_isError(ZL_CCtx_setCodecOutputCache(cctx, cache)));

    std::vector<uint32_t> input(16 * 1024);
    for (size_t i = 0; i < input.size(); ++i) {
        input[i] = (uint32_t)(i % 251);
    }
    ZL_TypedRef* const inputRef = ZL_TypedRef_createNumeric(
            input.data(), sizeof(input[0]), input.size());
    ASSERT_NE(inputRef, nullptr);
    std::vector<char> first(ZL_compressBound(input.size() * sizeof(input[0])));
    std::vector<char> second(first.size());
    const ZL_Report firstResult = ZL_CCtx_compressTypedRef(
            cctx, first.data(), first.size(), inputRef);
    ASSERT_FALSE(ZL_isError(firstResult));
    const CodecCache_Stats afterFirst = CodecCache_getStats(cache);
    EXPECT_GT(afterFirst.misses, 0);
    EXPECT_GT(afterFirst.inserts, 0);

    const ZL_Report secondResult = ZL_CCtx_compressTypedRef(
            cctx, second.data(), second.size(), inputRef);
    ASSERT_FALSE(ZL_isError(secondResult));
    ASSERT_EQ(ZL_validResult(firstResult), ZL_validResult(secondResult));
    EXPECT_EQ(
            std::memcmp(
                    first.data(), second.data(), ZL_validResult(firstResult)),
            0);
    const CodecCache_Stats afterSecond = CodecCache_getStats(cache);
    EXPECT_GT(afterSecond.hits, afterFirst.hits);
    EXPECT_GT(CodecCache_getHashReuses(cache), 0);

    ZL_TypedRef_free(inputRef);
    ZL_CCtx_free(cctx);
    ZL_CodecOutputCache_free(cache);
    ZL_Compressor_free(compressor);
}

TEST(CodecOutputCacheIntegrationTest, MultiInputCodecBypassesCache)
{
    ZL_Compressor* const compressor = ZL_Compressor_create();
    ASSERT_NE(compressor, nullptr);
    const ZL_GraphID successors[] = { ZL_GRAPH_STORE, ZL_GRAPH_STORE };
    const ZL_GraphID graph        = ZL_Compressor_registerStaticGraph_fromNode(
            compressor, ZL_NODE_CONCAT_SERIAL, successors, 2);
    ASSERT_TRUE(ZL_GraphID_isValid(graph));
    ASSERT_FALSE(
            ZL_isError(ZL_Compressor_selectStartingGraphID(compressor, graph)));

    ZL_CCtx* const cctx = ZL_CCtx_create();
    ASSERT_NE(cctx, nullptr);
    ASSERT_FALSE(ZL_isError(ZL_CCtx_refCompressor(cctx, compressor)));
    ASSERT_FALSE(ZL_isError(ZL_CCtx_setParameter(
            cctx, ZL_CParam_formatVersion, ZL_MAX_FORMAT_VERSION)));
    ZL_CodecOutputCache* const cache =
            ZL_CodecOutputCache_createWithBudget(1 << 20);
    ASSERT_NE(cache, nullptr);
    CodecCache_setStatsEnabled(cache, true);
    ASSERT_FALSE(ZL_isError(ZL_CCtx_setCodecOutputCache(cctx, cache)));

    const std::string firstData(1024, 'a');
    const std::string secondData(1024, 'b');
    ZL_TypedRef* const first =
            ZL_TypedRef_createSerial(firstData.data(), firstData.size());
    ZL_TypedRef* const second =
            ZL_TypedRef_createSerial(secondData.data(), secondData.size());
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    const ZL_TypedRef* inputs[] = { first, second };
    std::vector<char> compressed(
            2 * ZL_compressBound(firstData.size() + secondData.size()));
    const ZL_Report result = ZL_CCtx_compressMultiTypedRef(
            cctx, compressed.data(), compressed.size(), inputs, 2);
    ASSERT_FALSE(ZL_isError(result));

    const CodecCache_Stats stats = CodecCache_getStats(cache);
    EXPECT_EQ(stats.nonSingleInputSkips, 1);

    ZL_TypedRef_free(second);
    ZL_TypedRef_free(first);
    ZL_CCtx_free(cctx);
    ZL_CodecOutputCache_free(cache);
    ZL_Compressor_free(compressor);
}

} // namespace
