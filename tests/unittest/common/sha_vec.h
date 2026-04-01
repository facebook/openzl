// Copyright (c) Meta Platforms, Inc. and affiliates.

#pragma once

#include <stddef.h> // size_t

namespace openzl::tests {

/*
 * Standard testing vectors for SHA256.
 */
typedef struct {
    const void* input;
    size_t inputLen;
    const void* expected;
} SHA256_TestVector;

typedef struct {
    const SHA256_TestVector* vectors;
    size_t nbVectors;
} SHA256_TestVectorSet;

extern const SHA256_TestVectorSet NIST_testVectorSet;

} // namespace openzl::tests
