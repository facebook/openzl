// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/common/stream.h"
#include "openzl/zl_data.h"
#include "openzl/zl_errors.h"

#include "tests/utils.h"

namespace {

const ZL_DataID kZeroID = { 0 };
const ZL_DataID kOneID  = { 1 };

TEST(Stream, intMetadata)
{
    ZL_Data* const s = STREAM_create(kZeroID);
    ASSERT_NE(s, nullptr);

    ASSERT_ZS_VALID(ZL_Data_setIntMetadata(s, 1, 1001));
    ASSERT_ZS_VALID(ZL_Data_setIntMetadata(s, 2, 2002));

    ASSERT_EQ(ZL_Data_getIntMetadata(s, 1).isPresent, 1);
    ASSERT_EQ(ZL_Data_getIntMetadata(s, 2).isPresent, 1);
    ASSERT_EQ(ZL_Data_getIntMetadata(s, 1).mValue, 1001);
    ASSERT_EQ(ZL_Data_getIntMetadata(s, 2).mValue, 2002);

    // test what happens when requesting a non-present metadata id
    ASSERT_EQ(ZL_Data_getIntMetadata(s, 3).isPresent, 0);

    STREAM_free(s);
}

// check byteSize function
TEST(Stream, byteSize)
{
    ZL_Data* const s = STREAM_create(kZeroID);
    ASSERT_NE(s, nullptr);

    size_t const eltSize     = 4;
    size_t const eltCapacity = 20;
    ZL_Report const r = STREAM_reserve(s, ZL_Type_struct, eltSize, eltCapacity);
    ASSERT_EQ(ZL_isError(r), 0);

    // Requesting byteSize on a not yet committed stream
    ASSERT_EQ((int)STREAM_byteSize(s), 0);

    size_t const nbElts = 10;
    ASSERT_LE(nbElts, eltCapacity);
    ZL_REQUIRE_SUCCESS(ZL_Data_commit(s, nbElts));

    // Control size is correct after commit
    ASSERT_EQ((int)STREAM_byteSize(s), (int)(nbElts * eltSize));

    STREAM_free(s);
}

TEST(Stream, copyFixedSizeType)
{
    ZL_Data* const src = STREAM_create(kZeroID);
    ZL_Data* const dst = STREAM_create(kOneID);

    ZL_REQUIRE_SUCCESS(STREAM_reserve(src, ZL_Type_struct, 4, 10));
    memset(ZL_Data_wPtr(src), 0xFE, 10 * 4);
    ZL_REQUIRE_SUCCESS(ZL_Data_commit(src, 10));

    ZL_REQUIRE_SUCCESS(STREAM_copy(dst, src));

    ASSERT_EQ(ZL_Data_type(dst), ZL_Type_struct);
    ASSERT_EQ(ZL_Data_eltWidth(dst), 4u);
    ASSERT_EQ(ZL_Data_numElts(dst), 10u);
    ASSERT_NE(ZL_Data_rPtr(src), ZL_Data_rPtr(dst));

    ASSERT_EQ(memcmp(ZL_Data_rPtr(dst), ZL_Data_rPtr(src), 10 * 4), 0);

    STREAM_free(src);
    STREAM_free(dst);
}

TEST(Stream, copyString)
{
    ZL_Data* const src = STREAM_create(kZeroID);
    ZL_Data* const dst = STREAM_create(kOneID);

    ZL_REQUIRE_SUCCESS(STREAM_reserve(src, ZL_Type_string, 1, 20));
    uint32_t* const srcLens = ZL_Data_reserveStringLens(src, 4);
    ASSERT_NE(srcLens, nullptr);
    memset(ZL_Data_wPtr(src), 0xFE, 20);
    srcLens[0] = 5;
    srcLens[1] = 10;
    srcLens[2] = 5;
    ZL_REQUIRE_SUCCESS(ZL_Data_commit(src, 3));

    ZL_REQUIRE_SUCCESS(STREAM_copy(dst, src));

    ASSERT_EQ(ZL_Data_type(dst), ZL_Type_string);
    ASSERT_EQ(ZL_Data_eltWidth(dst), 0u);
    ASSERT_EQ(ZL_Data_numElts(dst), 3u);
    ASSERT_EQ(ZL_Data_contentSize(dst), 20u);
    ASSERT_NE(ZL_Data_rPtr(src), ZL_Data_rPtr(dst));
    ASSERT_NE(ZL_Data_rStringLens(src), ZL_Data_rStringLens(dst));

    ASSERT_EQ(memcmp(ZL_Data_rPtr(dst), ZL_Data_rPtr(src), 20), 0);
    const uint32_t* const lens = ZL_Data_rStringLens(dst);
    ASSERT_EQ(lens[0], 5u);
    ASSERT_EQ(lens[1], 10u);
    ASSERT_EQ(lens[2], 5u);

    STREAM_free(src);
    STREAM_free(dst);
}

TEST(Stream, refStream)
{
    ZL_Data* const ref = STREAM_create(kZeroID);
    ASSERT_NE(ref, nullptr);

    ZL_REQUIRE_SUCCESS(STREAM_reserve(ref, ZL_Type_numeric, 2, 3));
    ((uint16_t*)ZL_Data_wPtr(ref))[0] = 1;
    ((uint16_t*)ZL_Data_wPtr(ref))[1] = 2;
    ((uint16_t*)ZL_Data_wPtr(ref))[2] = 3;
    ZL_REQUIRE_SUCCESS(ZL_Data_commit(ref, 3));

    ASSERT_ZS_VALID(ZL_Data_setIntMetadata(ref, 1, 1001));
    ASSERT_ZS_VALID(ZL_Data_setIntMetadata(ref, 2, 2002));

    ZL_Data* const s = STREAM_create(kZeroID);
    ZL_REQUIRE_SUCCESS(STREAM_refStreamWithoutRefCount(s, ref));

    // Check that all fields are copied
    ASSERT_EQ(ZL_Data_type(s), ZL_Data_type(ref));
    ASSERT_EQ(ZL_Data_eltWidth(s), ZL_Data_eltWidth(ref));
    ASSERT_EQ(ZL_Data_numElts(s), ZL_Data_numElts(ref));

    ASSERT_EQ(ZL_Data_getIntMetadata(s, 1).isPresent, 1);
    ASSERT_EQ(ZL_Data_getIntMetadata(s, 2).isPresent, 1);
    ASSERT_EQ(ZL_Data_getIntMetadata(s, 1).mValue, 1001);
    ASSERT_EQ(ZL_Data_getIntMetadata(s, 2).mValue, 2002);

    ASSERT_EQ(memcmp(ZL_Data_rPtr(s), ZL_Data_rPtr(ref), 6), 0);

    // Check that if we add a new metadata to the ref, it's not copied
    ASSERT_ZS_VALID(ZL_Data_setIntMetadata(ref, 3, 3003));
    ASSERT_EQ(ZL_Data_getIntMetadata(s, 3).isPresent, 0);

    STREAM_free(s);
    STREAM_free(ref);
}

TEST(Stream, copyIntMetas)
{
    // Create source stream with int metadata
    ZL_Data* const src = STREAM_create(kZeroID);
    ASSERT_NE(src, nullptr);

    ZL_REQUIRE_SUCCESS(STREAM_reserve(src, ZL_Type_struct, 4, 10));
    ZL_REQUIRE_SUCCESS(ZL_Data_commit(src, 5));

    ASSERT_ZS_VALID(ZL_Data_setIntMetadata(src, 42, 100));
    ASSERT_ZS_VALID(ZL_Data_setIntMetadata(src, 7, 255));
    ASSERT_ZS_VALID(ZL_Data_setIntMetadata(src, 13, -50));

    // Create destination stream
    ZL_Data* const dst = STREAM_create(kOneID);
    ASSERT_NE(dst, nullptr);

    // Copy the stream
    ZL_REQUIRE_SUCCESS(STREAM_copy(dst, src));

    // Verify metadata was copied correctly
    ASSERT_EQ(ZL_Data_getIntMetadata(dst, 42).isPresent, 1);
    ASSERT_EQ(ZL_Data_getIntMetadata(dst, 7).isPresent, 1);
    ASSERT_EQ(ZL_Data_getIntMetadata(dst, 13).isPresent, 1);

    ASSERT_EQ(ZL_Data_getIntMetadata(dst, 42).mValue, 100);
    ASSERT_EQ(ZL_Data_getIntMetadata(dst, 7).mValue, 255);
    ASSERT_EQ(ZL_Data_getIntMetadata(dst, 13).mValue, -50);

    // Verify that changing metadata in source doesn't affect destination
    ASSERT_ZS_VALID(ZL_Data_setIntMetadata(src, 99, 999));
    ASSERT_EQ(ZL_Data_getIntMetadata(dst, 99).isPresent, 0);

    STREAM_free(src);
    STREAM_free(dst);
}

} // namespace
