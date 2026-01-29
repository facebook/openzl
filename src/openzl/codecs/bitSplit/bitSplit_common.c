// Copyright (c) Meta Platforms, Inc. and affiliates.

#include "openzl/codecs/bitSplit/bitSplit_common.h"
#include <assert.h>

size_t ZS_bitSplit_outputEltWidth(unsigned bitWidth)
{
    if (bitWidth <= 8) {
        return 1;
    }
    if (bitWidth <= 16) {
        return 2;
    }
    if (bitWidth <= 32) {
        return 4;
    }
    assert(bitWidth <= 64);
    return 8;
}
