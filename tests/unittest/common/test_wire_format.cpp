// Copyright (c) Meta Platforms, Inc. and affiliates.

#include <gtest/gtest.h>

#include "openzl/common/errors_internal.h"
#include "openzl/common/limits.h"
#include "openzl/common/wire_format.h"
#include "openzl/compress/encode_frameheader.h"
#include "openzl/decompress/decode_frameheader.h"
#include "openzl/shared/mem.h"

#include "tests/utils.h"

namespace {
uint32_t constexpr kMinVersion = ZL_MIN_FORMAT_VERSION;
uint32_t constexpr kMaxVersion = ZL_MAX_FORMAT_VERSION;
} // namespace

TEST(WireFormatTest, SupportedFormatVersions)
{
    ASSERT_LE(kMinVersion, kMaxVersion);
    for (uint32_t v = kMinVersion; v <= kMaxVersion; ++v) {
        ASSERT_TRUE(ZL_isFormatVersionSupported(v));
    }
}

TEST(WireFormatTest, MagicNumberToVersion)
{
    for (uint32_t v = kMinVersion; v <= kMaxVersion; ++v) {
        uint32_t const magic = ZL_getMagicNumber(v);
        ZL_Report const ret  = ZL_getFormatVersionFromMagic(magic);
        ASSERT_FALSE(ZL_isError(ret));
        ASSERT_EQ(ZL_validResult(ret), (size_t)v);
    }
}

TEST(WireFormatTest, MagicNumberFrameFormat)
{
    for (uint32_t v = kMinVersion; v <= kMaxVersion; ++v) {
        char buffer[4];
        ZL_writeMagicNumber(buffer, sizeof(buffer), v);
        ZL_Report const ret =
                ZL_getFormatVersionFromFrame(buffer, sizeof(buffer));
        ASSERT_FALSE(ZL_isError(ret));
        ASSERT_EQ(ZL_validResult(ret), (size_t)v);
    }
}

TEST(WireFormatTest, InvalidMagicNumberFrameFormat)
{
    const uint32_t tooOldMagic = ZSTRONG_MAGIC_NUMBER_BASE + kMinVersion - 1;
    const uint32_t tooNewMagic = ZSTRONG_MAGIC_NUMBER_BASE + kMaxVersion + 1;
    const uint32_t zstdMagic   = 0xFD2FB528u;

    const std::vector<std::pair<uint32_t, ZL_ErrorCode>> expectations{
        { { tooOldMagic, ZL_ErrorCode_formatVersion_unsupported },
          { tooNewMagic, ZL_ErrorCode_formatVersion_unsupported },
          { zstdMagic, ZL_ErrorCode_header_unknown } }
    };

    for (const auto& [magic, code] : expectations) {
        char buffer[4];
        ZL_writeLE32(buffer, magic);
        ZL_Report const ret =
                ZL_getFormatVersionFromFrame(buffer, sizeof(buffer));
        ASSERT_TRUE(ZL_isError(ret));
        ASSERT_EQ(ZL_E_code(ZL_RES_error(ret)), code);
    }
}

TEST(WireFormatTest, DefaultEncodingVersion)
{
    ASSERT_TRUE(ZL_isFormatVersionSupported(ZL_getDefaultEncodingVersion()));
}

TEST(WireFormatTest, CommentFrameFormat)
{
    Arena* arena      = ALLOC_StackArena_create();
    uint16_t configId = 300;
    EFH_FrameInfo fi;
    fi.comment.data          = (uint8_t*)&configId;
    fi.comment.size          = 2;
    fi.numInputs             = 1;
    ZL_FrameProperties fprop = {
        .hasContentChecksum    = false,
        .hasCompressedChecksum = false,
        .hasComment            = true,
    };
    InputDesc inputDesc;
    inputDesc.type     = ZL_Type_serial;
    inputDesc.numElts  = 100;
    inputDesc.byteSize = 100;
    fi.fprop           = &fprop;
    fi.inputDescs      = &inputDesc;

    size_t dstCapacity = 1000;

    void* dst = ALLOC_Arena_malloc(arena, dstCapacity);

    EXPECT_ZS_VALID(EFH_writeFrameHeader(
            dst, dstCapacity, &fi, ZL_COMMENT_VERSION_MIN));
    // Decode
    DFH_Struct* dfh =
            (DFH_Struct*)ALLOC_Arena_malloc(arena, sizeof(DFH_Struct));
    DFH_init(dfh);
    dfh->formatVersion = ZL_COMMENT_VERSION_MIN;
    EXPECT_ZS_VALID(DFH_decodeFrameHeader(dfh, dst, dstCapacity));
    auto comment = ZL_RES_value(ZL_FrameInfo_getComment(dfh->frameinfo));
    EXPECT_EQ(comment.size, 2);
    uint16_t result = *(const uint16_t*)(const void*)comment.data;
    EXPECT_EQ(result, configId);
    DFH_destroy(dfh);
    ALLOC_Arena_freeArena(arena);
}

TEST(WireFormatTest, DecodeOldFrameFormatWithComment)
{
    Arena* arena      = ALLOC_StackArena_create();
    uint16_t configId = 300;
    EFH_FrameInfo fi;
    fi.comment.data          = (uint8_t*)&configId;
    fi.comment.size          = 2;
    fi.numInputs             = 1;
    ZL_FrameProperties fprop = {
        .hasContentChecksum    = false,
        .hasCompressedChecksum = false,
        .hasComment            = true,
    };
    InputDesc inputDesc;
    inputDesc.type     = ZL_Type_serial;
    inputDesc.numElts  = 100;
    inputDesc.byteSize = 100;
    fi.fprop           = &fprop;
    fi.inputDescs      = &inputDesc;

    size_t dstCapacity = 1000;

    void* dst = ALLOC_Arena_malloc(arena, dstCapacity);

    EXPECT_ZS_VALID(EFH_writeFrameHeader(
            dst, dstCapacity, &fi, ZL_COMMENT_VERSION_MIN - 1));

    DFH_Struct* dfh =
            (DFH_Struct*)ALLOC_Arena_malloc(arena, sizeof(DFH_Struct));
    // Decode with newer version
    DFH_init(dfh);
    dfh->formatVersion = ZL_COMMENT_VERSION_MIN;
    EXPECT_ZS_VALID(DFH_decodeFrameHeader(dfh, dst, dstCapacity));
    EXPECT_ZS_ERROR(ZL_FrameInfo_getComment(dfh->frameinfo));
    // Decode with old version
    dfh->formatVersion = ZL_COMMENT_VERSION_MIN - 1;
    EXPECT_ZS_VALID(DFH_decodeFrameHeader(dfh, dst, dstCapacity));
    DFH_destroy(dfh);
    ALLOC_Arena_freeArena(arena);
}

TEST(WireFormatTest, CommentExceedingSizeLimitCannotBeEncoded)
{
    Arena* arena = ALLOC_StackArena_create();

    EFH_FrameInfo fi;
    fi.comment.size          = ZL_MAX_HEADER_COMMENT_SIZE_LIMIT + 1;
    fi.numInputs             = 1;
    ZL_FrameProperties fprop = {
        .hasContentChecksum    = false,
        .hasCompressedChecksum = false,
        .hasComment            = true,
    };
    InputDesc inputDesc;
    inputDesc.type     = ZL_Type_serial;
    inputDesc.numElts  = 100;
    inputDesc.byteSize = 100;
    fi.fprop           = &fprop;
    fi.inputDescs      = &inputDesc;
    size_t dstCapacity = 1000;
    void* dst          = ALLOC_Arena_malloc(arena, dstCapacity);

    EXPECT_ZS_ERROR(EFH_writeFrameHeader(
            dst, dstCapacity, &fi, ZL_COMMENT_VERSION_MIN - 1));
    ALLOC_Arena_freeArena(arena);
}
