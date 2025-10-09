// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/common/wire_format.h"

#include "openzl/shared/mem.h"

#define ZL_MIN_MAGIC (ZSTRONG_MAGIC_NUMBER_BASE + ZL_MIN_FORMAT_VERSION)
#define ZL_MAX_MAGIC (ZSTRONG_MAGIC_NUMBER_BASE + ZL_MAX_FORMAT_VERSION)

ZL_Report ZL_getFormatVersionFromFrame(void const* src, size_t srcSize)
{
    ZL_RET_R_IF_LT(srcSize_tooSmall, srcSize, 4);
    uint32_t const magic = ZL_readCE32(src);
    return ZL_getFormatVersionFromMagic(magic);
}

void ZL_writeMagicNumber(void* dst, size_t dstCapacity, uint32_t version)
{
    ZL_ASSERT_GE(dstCapacity, 4);
    ZL_ASSERT(ZL_isFormatVersionSupported(version));
    ZL_writeCE32(dst, ZL_getMagicNumber(version));
}

ZL_Report ZL_getFormatVersionFromMagic(uint32_t magic)
{
    // Detect invalid magic numbers - outside of the range of versions
    // we know about. Pad the top end of the range to handle versions added
    // after this library was shipped.
    if (magic < ZSTRONG_MAGIC_NUMBER_BASE || magic > ZL_MAX_MAGIC + 16)
        ZL_RET_R_ERR(header_unknown);

    // Detect magic numbers we used for older versions that we no longer
    // support or newer versions we don't yet support.
    ZL_RET_R_IF_LT(formatVersion_unsupported, magic, ZL_MIN_MAGIC);
    ZL_RET_R_IF_GT(formatVersion_unsupported, magic, ZL_MAX_MAGIC);

    // Extract the supported version number.
    uint32_t const version = magic - ZSTRONG_MAGIC_NUMBER_BASE;
    ZL_ASSERT(ZL_isFormatVersionSupported(version));

    return ZL_returnValue(version);
}

bool ZL_isFormatVersionSupported(uint32_t version)
{
    return version >= ZL_MIN_FORMAT_VERSION && version <= ZL_MAX_FORMAT_VERSION;
}

uint32_t ZL_getMagicNumber(uint32_t version)
{
    ZL_ASSERT(ZL_isFormatVersionSupported(version));
    return ZSTRONG_MAGIC_NUMBER_BASE + version;
}

unsigned ZL_getDefaultEncodingVersion(void)
{
    return ZL_MAX_FORMAT_VERSION;
}
