// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include <iomanip>

#include "openzl/zl_compressor_serialization.h"

#include "openzl/common/a1cbor_helpers.h"
#include "openzl/common/logging.h"
#include "openzl/compress/private_nodes.h"
#include "openzl/cpp/Compressor.hpp"
#include "openzl/cpp/Exception.hpp"
#include "openzl/dict/dict_constants.h"

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
        ZL_Compressor* const materialized,
        const std::vector<uint8_t>& fatBundle = {})
{
    std::unique_ptr<
            ZL_CompressorDeserializer,
            ZL_CompressorDeserializer_Deleter>
            deserializer{ ZL_CompressorDeserializer_create() };
    auto des_res = ZL_CompressorDeserializer_deserialize(
            deserializer.get(),
            materialized,
            serialized->data(),
            serialized->size(),
            fatBundle.empty() ? nullptr : fatBundle.data(),
            fatBundle.size());
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
        ZL_Compressor* const materialized,
        const std::vector<uint8_t>& fatBundle = {})
{
    auto ser      = serialize(compressor);
    auto ser_json = serialize_to_json(compressor);
    auto json     = convert_to_json(ser);
    // std::cerr << *json << std::endl;

    EXPECT_EQ(*ser_json, *json);

    deserialize(ser, materialized, fatBundle);
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

TEST_F(CompressorSerializationTest,
       DependenciesUseNullBundleIDWhenNoBundleRequired)
{
    auto compressor = compressor_.get();
    auto zstd_gid   = ZL_Compressor_registerZstdGraph_withLevel(compressor, 1);
    ZL_REQUIRE_SUCCESS(
            ZL_Compressor_selectStartingGraphID(compressor, zstd_gid));

    auto deps = get_deps(serialize(compressor), nullptr);

    EXPECT_FALSE(ZL_UniqueID_isValid(&deps.bundle_id.id));
}

TEST_F(CompressorSerializationTest, RejectsMismatchedFatBundle)
{
    auto compressorProducer = makeCompressorProducer();
    for (uint32_t i = 0; i < 1000; i++) {
        auto result = compressorProducer.make_multi(1, 1);
        if (result.fatBundle.empty()) {
            continue;
        }

        auto serialized  = serialize(result.full[0].get());
        auto wrongBundle = result.fatBundle;
        wrongBundle[4] ^= 0xff;

        std::unique_ptr<
                ZL_CompressorDeserializer,
                ZL_CompressorDeserializer_Deleter>
                deserializer{ ZL_CompressorDeserializer_create() };
        ZL_Report report = ZL_CompressorDeserializer_deserialize(
                deserializer.get(),
                materialized_.get(),
                serialized->data(),
                serialized->size(),
                wrongBundle.data(),
                wrongBundle.size());

        EXPECT_TRUE(ZL_isError(report));
        EXPECT_EQ(ZL_Compressor_getDictBundleID(materialized_.get()), nullptr);
        return;
    }

    FAIL() << "CompressorProducer did not generate a dict-backed compressor";
}

TEST_F(CompressorSerializationTest, RoundtripRandomGraphs)
{
    auto compressorProducer = makeCompressorProducer();
    for (uint32_t i = 0; i < 1000; i++) {
        auto result        = compressorProducer.make_multi(1, 3);
        auto original      = std::move(result.full[0]);
        auto intermediate1 = std::move(result.base[0]);
        auto intermediate2 = std::move(result.base[1]);
        auto final         = std::move(result.base[2]);
        const auto& fb     = result.fatBundle;

        auto json1 = roundtrip(original.get(), intermediate1.get(), fb);
        auto json2 = roundtrip(intermediate1.get(), intermediate2.get(), fb);
        auto json3 = roundtrip(intermediate2.get(), final.get(), fb);
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
    }
}

TEST_F(CompressorSerializationTest, RejectPrivateStoreAsParameterizedBase)
{
    //     openzl::Compressor compressor;
    //     // Create a parameterize the private serial_store graph and register
    //     it. const ZL_GraphID serialStoreGraph = {
    //         ZL_PrivateStandardGraphID_serial_store
    //     };
    //     const ZL_ParameterizedGraphDesc desc = {
    //         .name           = NULL,
    //         .graph          = serialStoreGraph,
    //         .customGraphs   = NULL,
    //         .nbCustomGraphs = 0,
    //         .customNodes    = NULL,
    //         .nbCustomNodes  = 0,
    //         .localParams    = NULL,
    //     };
    //     auto paramGid =
    //             ZL_Compressor_registerParameterizedGraph(compressor.get(),
    //             &desc);
    //     ASSERT_NE(paramGid.gid, ZL_GRAPH_ILLEGAL.gid);
    //     compressor.selectStartingGraph(paramGid);

    // Deserializing a parameterized private graph should fail. The bytes come
    // from a parameterized serial store graph if successfully executed.
    const unsigned char serializedBytes[] = {
        0xa6, 0x67, 0x76, 0x65, 0x72, 0x73, 0x69, 0x6f, 0x6e, 0x18, 0x68, 0x66,
        0x70, 0x61, 0x72, 0x61, 0x6d, 0x73, 0xa1, 0x70, 0x37, 0x34, 0x33, 0x64,
        0x66, 0x39, 0x34, 0x65, 0x65, 0x34, 0x63, 0x37, 0x38, 0x61, 0x32, 0x61,
        0xa2, 0x64, 0x69, 0x6e, 0x74, 0x73, 0xa0, 0x65, 0x62, 0x6c, 0x6f, 0x62,
        0x73, 0xa0, 0x65, 0x6e, 0x6f, 0x64, 0x65, 0x73, 0xa0, 0x66, 0x67, 0x72,
        0x61, 0x70, 0x68, 0x73, 0xa1, 0x78, 0x19, 0x7a, 0x6c, 0x2e, 0x70, 0x72,
        0x69, 0x76, 0x61, 0x74, 0x65, 0x2e, 0x73, 0x65, 0x72, 0x69, 0x61, 0x6c,
        0x5f, 0x73, 0x74, 0x6f, 0x72, 0x65, 0x23, 0x30, 0xa5, 0x64, 0x74, 0x79,
        0x70, 0x65, 0x6d, 0x70, 0x61, 0x72, 0x61, 0x6d, 0x65, 0x74, 0x65, 0x72,
        0x69, 0x7a, 0x65, 0x64, 0x64, 0x62, 0x61, 0x73, 0x65, 0x77, 0x7a, 0x6c,
        0x2e, 0x70, 0x72, 0x69, 0x76, 0x61, 0x74, 0x65, 0x2e, 0x73, 0x65, 0x72,
        0x69, 0x61, 0x6c, 0x5f, 0x73, 0x74, 0x6f, 0x72, 0x65, 0x66, 0x67, 0x72,
        0x61, 0x70, 0x68, 0x73, 0x80, 0x65, 0x6e, 0x6f, 0x64, 0x65, 0x73, 0x80,
        0x66, 0x70, 0x61, 0x72, 0x61, 0x6d, 0x73, 0x70, 0x37, 0x34, 0x33, 0x64,
        0x66, 0x39, 0x34, 0x65, 0x65, 0x34, 0x63, 0x37, 0x38, 0x61, 0x32, 0x61,
        0x65, 0x73, 0x74, 0x61, 0x72, 0x74, 0x78, 0x19, 0x7a, 0x6c, 0x2e, 0x70,
        0x72, 0x69, 0x76, 0x61, 0x74, 0x65, 0x2e, 0x73, 0x65, 0x72, 0x69, 0x61,
        0x6c, 0x5f, 0x73, 0x74, 0x6f, 0x72, 0x65, 0x23, 0x30, 0x6d, 0x67, 0x6c,
        0x6f, 0x62, 0x61, 0x6c, 0x5f, 0x70, 0x61, 0x72, 0x61, 0x6d, 0x73, 0x70,
        0x37, 0x34, 0x33, 0x64, 0x66, 0x39, 0x34, 0x65, 0x65, 0x34, 0x63, 0x37,
        0x38, 0x61, 0x32, 0x61
    };
    std::string serialized(
            reinterpret_cast<const char*>(serializedBytes),
            sizeof(serializedBytes));

    openzl::Compressor deserializedCompressor;
    EXPECT_THROW(
            {
                try {
                    deserializedCompressor.deserialize(serialized);
                } catch (const openzl::Exception& e) {
                    std::string msg = e.what();
                    EXPECT_NE(
                            msg.find(
                                    "The private store graph cannot be used as a base graph and parameterized"),
                            std::string::npos);
                    throw;
                }
            },
            openzl::Exception);
}

} // namespace tests
} // namespace openzl
