// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

namespace openzl::lz {
enum class CustomCodecIDs {
    LZ       = 0x1337,
    SmallInt = 0x1338,
    VarByte  = 0x1339,
    IsoByte  = 0x133A,
    Bucket   = 0x133B,
    Bucket16 = 0x133C,
};
}
