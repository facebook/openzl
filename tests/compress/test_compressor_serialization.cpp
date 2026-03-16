// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/zl_compressor_serialization.h"

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/logging.h"

#include "tests/datagen/random_producer/PRNGWrapper.h"
#include "tests/datagen/structures/CompressorProducer.h"

#include "tests/utils.h"

using namespace ::testing;

namespace openzl {
namespace tests {

namespace {

datagen::CompressorProducer makeCompressorProducer()
{
    auto gen = std::make_shared<std::mt19937>(0xdeadbeef);
    auto rw  = std::make_shared<datagen::PRNGWrapper>(gen);
    return datagen::CompressorProducer{ rw };
}

struct ZS2_Compressor_Deleter {
    void operator()(ZL_Compressor* compressor)
    {
        ZL_Compressor_free(compressor);
    }
};

/**
 * Custom deleter for buffers allocated with malloc.
 *
 * This deleter is used with smart pointers to properly free memory that was
 * allocated with malloc or similar C allocation functions. The default deleter
 * would call delete, which is incorrect for malloc'd memory and would cause
 * undefined behavior. This deleter ensures free() is called instead.
 */
struct MallocedBuffer_Deleter {
    void operator()(const char* buf)
    {
        free((void*)(uintptr_t)buf);
    }
};

struct ZL_CompressorSerializer_Deleter {
    void operator()(ZL_CompressorSerializer* serializer)
    {
        ZL_CompressorSerializer_free(serializer);
    }
};

struct ZL_CompressorDeserializer_Deleter {
    void operator()(ZL_CompressorDeserializer* deserializer)
    {
        ZL_CompressorDeserializer_free(deserializer);
    }
};

class CompressorSerializationTest : public Test {
   protected:
    void SetUp() override
    {
        compressor_ = std::unique_ptr<ZL_Compressor, ZS2_Compressor_Deleter>{
            ZL_Compressor_create()
        };
        materialized_ = std::unique_ptr<ZL_Compressor, ZS2_Compressor_Deleter>{
            ZL_Compressor_create()
        };
    }

    std::unique_ptr<ZL_Compressor, ZS2_Compressor_Deleter> compressor_;
    std::unique_ptr<ZL_Compressor, ZS2_Compressor_Deleter> materialized_;
};

struct SerialiedGraphBundle {
    std::unique_ptr<ZL_CompressorSerializer, ZL_CompressorSerializer_Deleter>
            serializer;
    std::string_view serialized;
};

std::shared_ptr<const std::string_view> serialize(
        const ZL_Compressor* const compressor)
{
    std::unique_ptr<ZL_CompressorSerializer, ZL_CompressorSerializer_Deleter>
            serializer{ ZL_CompressorSerializer_create() };
    void* ser_ptr   = NULL;
    size_t ser_size = 0;
    auto ser_res    = ZL_CompressorSerializer_serialize(
            serializer.get(), compressor, &ser_ptr, &ser_size);
    if (ZL_RES_isError(ser_res)) {
        const auto msg = ZL_CompressorSerializer_getErrorContextString(
                serializer.get(), ser_res);
        std::cerr << msg << std::endl;
    }
    ZL_REQUIRE_SUCCESS(ser_res);
    auto bundle = std::make_shared<std::pair<
            std::unique_ptr<
                    ZL_CompressorSerializer,
                    ZL_CompressorSerializer_Deleter>,
            std::string_view>>(
            std::move(serializer),
            std::string_view(static_cast<const char*>(ser_ptr), ser_size));
    auto str_view_ptr = &bundle->second;
    return std::shared_ptr<const std::string_view>(
            std::move(bundle), str_view_ptr);
}

std::shared_ptr<const std::string_view> serialize_to_json(
        const ZL_Compressor* const compressor)
{
    std::unique_ptr<ZL_CompressorSerializer, ZL_CompressorSerializer_Deleter>
            serializer{ ZL_CompressorSerializer_create() };
    void* ser_ptr   = NULL;
    size_t ser_size = 0;
    auto ser_res    = ZL_CompressorSerializer_serializeToJson(
            serializer.get(), compressor, &ser_ptr, &ser_size);
    if (ZL_RES_isError(ser_res)) {
        const auto msg = ZL_CompressorSerializer_getErrorContextString(
                serializer.get(), ser_res);
        std::cerr << msg << std::endl;
    }
    ZL_REQUIRE_SUCCESS(ser_res);
    auto bundle = std::make_shared<std::pair<
            std::unique_ptr<
                    ZL_CompressorSerializer,
                    ZL_CompressorSerializer_Deleter>,
            std::string_view>>(
            std::move(serializer),
            std::string_view(static_cast<const char*>(ser_ptr), ser_size));
    auto str_view_ptr = &bundle->second;
    return std::shared_ptr<const std::string_view>(
            std::move(bundle), str_view_ptr);
}

std::shared_ptr<const std::string_view> convert_to_json(
        const std::shared_ptr<const std::string_view>& serialized)
{
    std::unique_ptr<ZL_CompressorSerializer, ZL_CompressorSerializer_Deleter>
            serializer{ ZL_CompressorSerializer_create() };
    void* dst      = nullptr;
    size_t dstSize = 0;
    ZL_REQUIRE_SUCCESS(ZL_CompressorSerializer_convertToJson(
            serializer.get(),
            &dst,
            &dstSize,
            serialized->data(),
            serialized->size()));

    auto bundle = std::make_shared<std::pair<
            std::unique_ptr<
                    ZL_CompressorSerializer,
                    ZL_CompressorSerializer_Deleter>,
            std::string_view>>(
            std::move(serializer),
            std::string_view(static_cast<const char*>(dst), dstSize));
    auto str_view_ptr = &bundle->second;
    return std::shared_ptr<const std::string_view>(
            std::move(bundle), str_view_ptr);
}

void deserialize(
        const std::shared_ptr<const std::string_view>& serialized,
        ZL_Compressor* const materialized)
{
    std::unique_ptr<
            ZL_CompressorDeserializer,
            ZL_CompressorDeserializer_Deleter>
            deserializer{ ZL_CompressorDeserializer_create() };
    auto des_res = ZL_CompressorDeserializer_deserialize(
            deserializer.get(),
            materialized,
            serialized->data(),
            serialized->size());
    if (ZL_RES_isError(des_res)) {
        const auto msg = ZL_CompressorDeserializer_getErrorContextString(
                deserializer.get(), des_res);
        std::cerr << msg << std::endl;
    }
    ZL_REQUIRE_SUCCESS(des_res);
}

ZL_CompressorDeserializer_Dependencies get_deps(
        const std::shared_ptr<const std::string_view>& serialized,
        const ZL_Compressor* const materialized)
{
    std::unique_ptr<
            ZL_CompressorDeserializer,
            ZL_CompressorDeserializer_Deleter>
            deserializer{ ZL_CompressorDeserializer_create() };
    auto des_res = ZL_CompressorDeserializer_getDependencies(
            deserializer.get(),
            materialized,
            serialized->data(),
            serialized->size());
    if (ZL_RES_isError(des_res)) {
        const auto msg =
                ZL_CompressorDeserializer_getErrorContextString_fromError(
                        deserializer.get(), ZL_RES_error(des_res));
        std::cerr << msg << std::endl;
    }
    ZL_REQUIRE_SUCCESS(des_res);
    auto deps = ZL_RES_value(des_res);
    // std::cerr << deps.num_graphs << " graphs:" << std::endl;
    // for (size_t j = 0; j < deps.num_graphs; j++) {
    //     std::cerr << "  " << deps.graph_names[j] << std::endl;
    // }
    // std::cerr << deps.num_nodes << " nodes:" << std::endl;
    // for (size_t j = 0; j < deps.num_nodes; j++) {
    //     std::cerr << "  " << deps.node_names[j] << std::endl;
    // }
    return deps;
}

std::string roundtrip(
        const ZL_Compressor* const compressor,
        ZL_Compressor* const materialized)
{
    auto ser      = serialize(compressor);
    auto ser_json = serialize_to_json(compressor);
    auto json     = convert_to_json(ser);
    // std::cerr << *json << std::endl;

    EXPECT_EQ(*ser_json, *json);

    deserialize(ser, materialized);
    return std::string{ *json };
}

} // anonymous namespace

TEST_F(CompressorSerializationTest, CustomZstd)
{
    auto compressor = compressor_.get();
    auto zstd_gid   = ZL_Compressor_registerZstdGraph_withLevel(compressor, 1);
    ZL_REQUIRE_SUCCESS(
            ZL_Compressor_selectStartingGraphID(compressor, zstd_gid));

    roundtrip(compressor, materialized_.get());
}

TEST_F(CompressorSerializationTest, Roundtrip)
{
    auto compressor = compressor_.get();
    auto zstd_gid   = ZL_Compressor_registerZstdGraph_withLevel(compressor, 1);

    std::vector<ZL_IntParam> ips;
    std::vector<ZL_CopyParam> cps;
    cps.push_back((ZL_CopyParam){
            .paramId   = 1234,
            .paramPtr  = "foo\0bar",
            .paramSize = 7,
    });
    const auto make_lp = [&]() {
        return (ZL_LocalParams){
            .intParams =
                    (ZL_LocalIntParams){
                            .intParams   = ips.data(),
                            .nbIntParams = ips.size(),
                    },
            .copyParams =
                    (ZL_LocalCopyParams){
                            .copyParams   = cps.data(),
                            .nbCopyParams = cps.size(),
                    },
        };
    };
    auto lp                               = make_lp();
    const ZL_ParameterizedNodeDesc pndesc = {
        .node        = ZL_NODE_ZIGZAG,
        .localParams = &lp,
    };
    auto cp_nid = ZL_Compressor_registerParameterizedNode(compressor, &pndesc);
    EXPECT_NE(cp_nid, ZL_NODE_ILLEGAL);

    ips.push_back((ZL_IntParam){
            .paramId    = 123,
            .paramValue = 5678,
    });
    lp                                     = make_lp();
    const ZL_ParameterizedNodeDesc pndesc2 = {
        .node        = cp_nid,
        .localParams = &lp,
    };
    auto cp_cp_nid =
            ZL_Compressor_registerParameterizedNode(compressor, &pndesc2);
    EXPECT_NE(cp_cp_nid, ZL_NODE_ILLEGAL);

    ZL_REQUIRE_SUCCESS(
            ZL_Compressor_selectStartingGraphID(compressor, zstd_gid));

    roundtrip(compressor, materialized_.get());
}

TEST_F(CompressorSerializationTest, RoundtripRandomGraphs)
{
    auto compressorProducer = makeCompressorProducer();
    for (uint32_t i = 0; i < 1000; i++) {
        auto compressors   = compressorProducer.make_multi(1, 3);
        auto original      = std::move(compressors.first[0]);
        auto intermediate1 = std::move(compressors.second[0]);
        auto intermediate2 = std::move(compressors.second[1]);
        auto final         = std::move(compressors.second[2]);

        auto json1 = roundtrip(original.get(), intermediate1.get());
        auto json2 = roundtrip(intermediate1.get(), intermediate2.get());
        auto json3 = roundtrip(intermediate2.get(), final.get());
        (void)json1;
        (void)json2;
        (void)json3;
        // ASSERT_EQ(json2, json3);
        // std::cerr << std::endl;
    }
}

TEST_F(CompressorSerializationTest, GetDepsWithNULL)
{
    auto compressorProducer = makeCompressorProducer();
    for (uint32_t i = 0; i < 1000; i++) {
        auto compressor = compressorProducer.make();
        auto ser        = serialize(compressor.get());
        auto json       = convert_to_json(ser);
        (void)json;
        auto deps = get_deps(ser, NULL);
        (void)deps;
        // std::cerr << *json << std::endl;
        // std::cerr << std::endl;
    }
}

TEST_F(CompressorSerializationTest, GetDepsWithCompressor)
{
    auto compressorProducer = makeCompressorProducer();
    for (uint32_t i = 0; i < 1000; i++) {
        auto compressor = compressorProducer.make();
        auto ser        = serialize(compressor.get());
        auto json       = convert_to_json(ser);
        (void)json;
        auto deps = get_deps(ser, compressor_.get());
        (void)deps;
        // std::cerr << *json << std::endl;
        // std::cerr << std::endl;
    }
}

} // namespace tests
} // namespace openzl
