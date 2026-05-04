// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <gtest/gtest.h>

#include <algorithm>
#include <numeric>
#include <random>
#include <set>
#include <vector>

#include "openzl/codecs/zl_clustering.h"
#include "openzl/compress/graphs/sddl2/sddl2_vm.h"
#include "openzl/cpp/CompressIntrospectionHooks.hpp"
#include "openzl/cpp/DecompressIntrospectionHooks.hpp"
#include "openzl/zl_config.h"
#include "openzl/zl_graph_api.h"
#include "openzl/zl_input.h"

namespace openzl {
namespace sddl2 {
namespace testing {

#if ZL_ALLOW_INTROSPECTION
class CompressChunkCounterHook : public openzl::CompressIntrospectionHooks {
   public:
    size_t chunkCount = 0;

    void on_ZL_Segmenter_processChunk_start(
            ZL_Segmenter*,
            const size_t[],
            size_t,
            ZL_GraphID,
            const ZL_RuntimeGraphParameters*) override
    {
        ++chunkCount;
    }
};

class ClusteringTagCaptureHook : public openzl::CompressIntrospectionHooks {
   public:
    std::set<int> tags;

    void on_migraphEncode_start(
            ZL_Graph*,
            const ZL_Compressor*,
            ZL_GraphID,
            ZL_Edge* inputs[],
            size_t nbInputs) override
    {
        for (size_t i = 0; i < nbInputs; ++i) {
            const ZL_Input* input = ZL_Edge_getData(inputs[i]);
            if (input == nullptr) {
                continue;
            }
            ZL_IntMetadata meta = ZL_Input_getIntMetadata(
                    input, ZL_CLUSTERING_TAG_METADATA_ID);
            if (meta.isPresent) {
                tags.insert(meta.mValue);
            }
        }
    }
};

class DecompressChunkCounterHook : public openzl::DecompressIntrospectionHooks {
   public:
    size_t chunkCount = 0;

    void on_decompressChunk_start(ZL_DCtx*, size_t) override
    {
        ++chunkCount;
    }
};
#endif

class SDDL2TestBase : public ::testing::Test {
   protected:
    static void* alloc_fn(void* allocator_ctx, size_t size)
    {
        auto arena = (std::vector<std::string>*)allocator_ctx;
        arena->push_back(std::string(size, 'x'));
        return arena->back().data();
    }

    template <typename T>
    static std::vector<T> gen(
            size_t length,
            std::optional<T> opt_min = std::nullopt,
            std::optional<T> opt_max = std::nullopt)
    {
        std::vector<T> vec(length);

        // Generate distribution
        T min = opt_min.value_or(std::numeric_limits<T>::lowest());
        T max = opt_max.value_or(std::numeric_limits<T>::max());
        std::uniform_int_distribution<T> dist(min, max);

        std::mt19937 mersenne_engine(10);
        auto gen = [&dist, &mersenne_engine]() {
            return dist(mersenne_engine);
        };

        std::generate(vec.begin(), vec.end(), gen);
        return vec;
    }

    static size_t sum(const std::vector<size_t>& vec)
    {
        return std::accumulate(vec.begin(), vec.end(), size_t{ 0 });
    }

    std::vector<std::string> arena_;
    void* alloc_ctx_ = &arena_;
};

template <size_t StackCapacity>
class SDDL2StackTestCustomCapacity : public SDDL2TestBase {
   protected:
    void SetUp() override
    {
        stack_ = (SDDL2_Stack*)malloc(sizeof(SDDL2_Stack));
        assert(stack_ != NULL);

        stack_->items =
                (SDDL2_Value*)malloc(sizeof(SDDL2_Value) * StackCapacity);
        assert(stack_->items != NULL);

        stack_->capacity = StackCapacity;
        SDDL2_Stack_init(stack_);
    }
    void TearDown() override
    {
        free(stack_->items);
        free(stack_);
    }

    SDDL2_Stack* stack_;
};

using SDDL2StackTest = SDDL2StackTestCustomCapacity<100>;

inline size_t getKindSize(SDDL2_Type_kind kind)
{
    SDDL2_RESULT_OF(size_t) result = SDDL2_kind_size(kind);
    EXPECT_FALSE(SDDL2_isError(result));
    return SDDL2_value(result);
}

inline size_t getTypeSize(SDDL2_Type type)
{
    SDDL2_RESULT_OF(size_t) result = SDDL2_Type_size(type);
    EXPECT_FALSE(SDDL2_isError(result));
    return SDDL2_value(result);
}

/**
 * Pop helper that extracts value into out parameter.
 * Provides compatibility with old API for tests.
 */
inline SDDL2_Error popValue(SDDL2_Stack* stack, SDDL2_Value* out)
{
    SDDL2_RESULT_OF(SDDL2_Value) result = SDDL2_Stack_pop(stack);
    if (SDDL2_isError(result)) {
        return SDDL2_error(result);
    }
    *out = SDDL2_value(result);
    return SDDL2_OK;
}

static void popAndVerifyI64(SDDL2_Stack* stack, int64_t expected)
{
    SDDL2_Value result;
    ASSERT_EQ(popValue(stack, &result), SDDL2_OK);
    EXPECT_EQ(result.kind, SDDL2_VALUE_I64);
    EXPECT_EQ(result.value.as_i64, expected);
}

} // namespace testing
} // namespace sddl2
} // namespace openzl
