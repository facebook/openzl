// Copyright (c) Meta Platforms, Inc. and affiliates.

// Compile-time check that OpenZL's public headers remain usable from a strict
// C99 translation unit. The body is intentionally trivial — building this
// target IS the test. Guards against regressions like the duplicate-typedef
// issue fixed in PR #293 (typedef redefinition is a C11 feature).

#include "openzl/openzl.h"

int main(void)
{
    return 0;
}
